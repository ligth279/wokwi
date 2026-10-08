#include "fault_fw.h"
#include "fault_inject.h"
#include "fi_port.h"
#include "log.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FI_MAX_FAULTS     16u
#define FI_MAX_EVENTS     8u
#define OBSERVE_WINDOW_MS 1000u  /* INJECTED -> effect must be seen within this */
#define TIMER_GRACE_MS    1000u  /* ARMED(timer) -> must fire by delay + this    */
#define GDB_WAIT_MS       10000u /* ARMED(gdb) -> GDB must inject within this    */
#define CYCLES_PER_MS     (72000000u / 1000u)

typedef enum {
    R_NONE = 0,
    R_NOT_IMPLEMENTED,
    R_EFFECT_NOT_OBSERVED,
    R_TIMER_NOT_FIRED,
    R_GDB_TIMEOUT,
    R_INTERRUPTED_BY_RESET,
    R_MECH_UNSUPPORTED,
} fi_reason_t;

typedef struct {
    uint8_t  state;
    uint8_t  reason;
    uint32_t cycle;
    uint32_t t_ms;
    uint32_t a; /* state-specific (see fi_log_pending) */
    uint32_t b;
} fi_event_t;

typedef struct {
    const fault_desc_t *fault;
    fi_mech_t           mech;
    uint32_t            seq;
    volatile uint8_t    state;
    volatile uint32_t   inject_count;
    uint32_t            delay_ms;
    uint32_t            armed_cycle, armed_t_ms;
    uint32_t            target_cycle; /* timer: intended injection cycle */
    uint32_t            inject_cycle, inject_t_ms;
    fi_event_t          ev[FI_MAX_EVENTS];
    volatile uint32_t   n_ev;
    uint32_t            n_logged;
} fi_exp_t;

/* Survives system resets (not power cycles). Validated by magic + checksum. */
typedef struct {
    uint32_t magic;
    uint16_t seq[FI_MAX_FAULTS];
    uint8_t  active;    /* an experiment had been started */
    uint8_t  fault_idx;
    uint8_t  mech;
    uint8_t  state;     /* last recorded lifecycle state */
    uint32_t exp_seq;
    uint32_t check;
} fi_persist_t;

#define FI_PERSIST_MAGIC 0xF1E7A5E5u

static fi_persist_t persist __attribute__((section(".noinit")));
static fi_exp_t cur;
char fi_cur_exp_id[24] = "none";
volatile uint32_t fi_gdb_mailbox;
volatile uint32_t fi_gdb_req_magic __attribute__((section(".noinit")));
char fi_gdb_req_id[16] __attribute__((section(".noinit")));
static char gdb_req_copy[16];
volatile uint32_t fi_gdb_anchor_cycle;
static volatile uint32_t timer_spurious;

static const char *const state_name[] = {"NONE", "SELECTED", "ARMED", "INJECTED", "OBSERVED", "COMPLETED", "ERROR"};
static const char *const mech_name[] = {"UART", "TIMER", "GDB"};
static const char *const reason_name[] = {"none", "not_implemented", "effect_not_observed",
                                          "timer_not_fired", "gdb_timeout", "interrupted_by_reset",
                                          "mechanism_unsupported"};

/* ---- persistence --------------------------------------------------------- */

static uint32_t persist_sum(const fi_persist_t *p)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t s = 0x1234u;
    for (size_t i = 0; i < offsetof(fi_persist_t, check); i++) {
        s = (s * 31u) + b[i];
    }
    return s;
}

static void persist_save(void)
{
    persist.check = persist_sum(&persist);
}

static unsigned fault_index(const fault_desc_t *f)
{
    return (unsigned)(f - fault_catalog);
}

/* ---- events -------------------------------------------------------------- */

static int is_terminal(uint8_t st)
{
    return st == FI_ST_COMPLETED || st == FI_ST_ERROR;
}

/* Record a lifecycle transition. Called from the owning context only (the
 * state machine guarantees a single writer per transition). */
static void emit(uint8_t state, uint8_t reason, uint32_t cycle, uint32_t a, uint32_t b)
{
    uint32_t n = cur.n_ev;
    if (n < FI_MAX_EVENTS) {
        fi_event_t *e = &cur.ev[n];
        e->state = state;
        e->reason = reason;
        e->cycle = cycle;
        e->t_ms = fi_port_ms();
        e->a = a;
        e->b = b;
        cur.n_ev = n + 1u; /* publish after the event is complete */
    }
    cur.state = state;
    persist.state = state;
    persist_save();
}

/* ---- injection ----------------------------------------------------------- */

/* Record the injection: stamp, count, INJECTED event. */
static void commit_injected(uint32_t cycle, uint32_t before, uint32_t after)
{
    cur.inject_cycle = cycle;
    cur.inject_t_ms = fi_port_ms();
    cur.inject_count++;
    g_faults_injected++;
    emit(FI_ST_INJECTED, R_NONE, cycle, before, after);
}

/* Perform the injection exactly once: only called while state == ARMED by
 * the single context that owns this experiment's mechanism, and the state
 * leaves ARMED in the same call.
 *
 * A FATAL fault may never return from inject() (crash, hang), so for those
 * the INJECTED event is recorded and printed synchronously first, from the
 * values the fault's plan() reports; inject() is called afterwards. */
static void do_inject(void)
{
    uint32_t before, after;
    uint32_t c = fi_port_cycles();
    if (cur.fault->flags & FAULT_F_FATAL) {
        cur.fault->plan(&before, &after);
        commit_injected(c, before, after);
        fi_log_pending();
        cur.fault->inject(&before, &after);
        return;
    }
    cur.fault->inject(&before, &after);
    commit_injected(c, before, after);
}

static void finish_error(fi_reason_t r)
{
    if (cur.fault->cleanup != NULL && cur.state >= FI_ST_INJECTED) {
        cur.fault->cleanup();
    }
    emit(FI_ST_ERROR, (uint8_t)r, fi_port_cycles(), cur.inject_count, 0);
}

/* ---- timer mechanism ------------------------------------------------------ */

/* Called once from the port's one-shot timer interrupt. Injects only if this
 * experiment is a TIMER experiment that is still ARMED; anything else is
 * counted and ignored, so a stray or late interrupt can never inject. */
void fi_timer_expired(void)
{
    if (cur.fault != NULL && cur.mech == FI_MECH_TIMER && cur.state == FI_ST_ARMED) {
        do_inject();
    } else {
        timer_spurious++;
    }
}

/* ---- GDB anchor ----------------------------------------------------------- */

/* GDB places a breakpoint here. Kept out of line and non-empty so the
 * call (and the breakpoint address) survives optimisation. */
#ifdef FI_HOST_TEST
void (*fi_host_gdb_hook)(void); /* host test plays the debugger here */
#endif

__attribute__((noinline)) void fi_gdb_anchor(void)
{
#ifdef FI_HOST_TEST
    if (fi_host_gdb_hook != NULL) {
        fi_host_gdb_hook();
    }
#else
    __asm volatile("nop" ::: "memory");
#endif
}

/* ---- public API ----------------------------------------------------------- */

#ifdef FI_HOST_TEST
/* Simulate a system reset: RAM state is lost, .noinit survives. */
void fi_host_reset(void)
{
    memset(&cur, 0, sizeof cur);
    strcpy(fi_cur_exp_id, "none");
    fi_gdb_mailbox = 0;
}
#endif

void fi_boot(void)
{
    fi_port_init();
    if (persist.magic != FI_PERSIST_MAGIC || persist.check != persist_sum(&persist)) {
        memset(&persist, 0, sizeof persist);
        persist.magic = FI_PERSIST_MAGIC;
        persist_save();
        LOG("FAULT", "framework=ready persist=cold");
        return;
    }
    if (persist.active && !is_terminal(persist.state) && persist.fault_idx < fault_catalog_count) {
        LOG("FAULT", "EXP=%s_%03lu state=ERROR reason=interrupted_by_reset last_state=%s mech=%s",
            fault_catalog[persist.fault_idx].id, (unsigned long)persist.exp_seq,
            persist.state < 7u ? state_name[persist.state] : "?", persist.mech < 3u ? mech_name[persist.mech] : "?");
        persist.state = FI_ST_ERROR;
        persist_save();
    }
    LOG("FAULT", "framework=ready persist=warm");
}

fi_select_t fi_select(const fault_desc_t *f, fi_mech_t mech, uint32_t delay_ms)
{
    if (cur.fault != NULL && !is_terminal(cur.state)) {
        return FI_SELECT_BUSY;
    }
    unsigned idx = fault_index(f);

    memset(&cur, 0, sizeof cur);
    cur.fault = f;
    cur.mech = mech;
    cur.delay_ms = delay_ms;
    cur.seq = ++persist.seq[idx];
    snprintf(fi_cur_exp_id, sizeof fi_cur_exp_id, "%s_%03lu", f->id, (unsigned long)cur.seq);
    persist.active = 1;
    persist.fault_idx = (uint8_t)idx;
    persist.mech = (uint8_t)mech;
    persist.exp_seq = cur.seq;

    emit(FI_ST_SELECTED, R_NONE, fi_port_cycles(), 0, 0);
    if (f->inject == NULL) {
        emit(FI_ST_ERROR, R_NOT_IMPLEMENTED, fi_port_cycles(), 0, 0);
        return FI_SELECT_OK;
    }
    if (mech == FI_MECH_GDB && (f->flags & FAULT_F_NO_GDB)) {
        emit(FI_ST_ERROR, R_MECH_UNSUPPORTED, fi_port_cycles(), 0, 0);
        return FI_SELECT_OK;
    }

    cur.armed_cycle = fi_port_cycles();
    cur.armed_t_ms = fi_port_ms();
    if (mech == FI_MECH_TIMER) {
        cur.target_cycle = cur.armed_cycle + delay_ms * CYCLES_PER_MS;
        emit(FI_ST_ARMED, R_NONE, cur.armed_cycle, delay_ms, cur.target_cycle);
        fi_port_timer_start(delay_ms); /* after ARMED is visible to the ISR */
    } else {
        fi_gdb_mailbox = 0;
        emit(FI_ST_ARMED, R_NONE, cur.armed_cycle, 0, 0);
    }
    return FI_SELECT_OK;
}

/* INJECTED -> OBSERVED -> COMPLETED, or ERROR when the effect does not show
 * within OBSERVE_WINDOW_MS. A fault without an observe() routine (FATAL:
 * the firmware cannot report its own crash) stays INJECTED; its effect is
 * established from the outside (log silence, debugger). */
static void observe_step(uint32_t now)
{
    if (cur.fault->observe == NULL) {
        return;
    }
    if (cur.fault->observe()) {
        emit(FI_ST_OBSERVED, R_NONE, fi_port_cycles(), 0, 0);
        if (cur.fault->cleanup != NULL) {
            cur.fault->cleanup();
        }
        emit(FI_ST_COMPLETED, R_NONE, fi_port_cycles(), cur.inject_count, 0);
    } else if (now - cur.inject_t_ms > OBSERVE_WINDOW_MS) {
        finish_error(R_EFFECT_NOT_OBSERVED);
    }
}

/* Control-task hook, called once at the start of every control cycle:
 * UART injection point, GDB anchor, observation and timeouts. */
void fi_site_control(void)
{
    if (cur.fault == NULL || is_terminal(cur.state)) {
        return;
    }
    uint32_t now = fi_port_ms();

    if (cur.state == FI_ST_ARMED) {
        switch (cur.mech) {
        case FI_MECH_UART:
            do_inject();
            break;
        case FI_MECH_GDB: {
            uint32_t before = cur.fault->read_target ? cur.fault->read_target() : 0;
            fi_gdb_anchor_cycle = fi_port_cycles();
            fi_gdb_anchor(); /* GDB halts here, injects, sets the mailbox */
            if (fi_gdb_mailbox == FI_GDB_DONE) {
                fi_gdb_mailbox = 0;
                uint32_t after = cur.fault->read_target ? cur.fault->read_target() : 0;
                cur.inject_cycle = fi_gdb_anchor_cycle;
                cur.inject_t_ms = fi_port_ms();
                cur.inject_count++;
                g_faults_injected++;
                emit(FI_ST_INJECTED, R_NONE, cur.inject_cycle, before, after);
            } else if (now - cur.armed_t_ms > GDB_WAIT_MS) {
                finish_error(R_GDB_TIMEOUT);
            }
            break;
        }
        case FI_MECH_TIMER:
            if (now - cur.armed_t_ms > cur.delay_ms + TIMER_GRACE_MS) {
                fi_port_timer_stop();
                finish_error(R_TIMER_NOT_FIRED);
            }
            break;
        }
        return;
    }

    if (cur.state == FI_ST_INJECTED && !(cur.fault->flags & FAULT_F_OBS_CONSOLE)) {
        observe_step(now);
    }
}

/* Console-task hook: observation of faults flagged FAULT_F_OBS_CONSOLE
 * (those after which the control cycle no longer runs). */
void fi_poll(void)
{
    if (cur.fault != NULL && cur.state == FI_ST_INJECTED && (cur.fault->flags & FAULT_F_OBS_CONSOLE)) {
        observe_step(fi_port_ms());
    }
}

void fi_log_pending(void)
{
    /* Events are printed by the console task and, for FATAL faults, by the
     * injecting task; the lock keeps each event printed once and in order. */
    int lock = fi_port_log_lock();
    while (cur.fault != NULL && cur.n_logged < cur.n_ev) {
        const fi_event_t *e = &cur.ev[cur.n_logged];
        const char *st = state_name[e->state];
        switch (e->state) {
        case FI_ST_SELECTED:
            LOG("FAULT", "EXP=%s state=%s fault=%s class=%s name=%s mech=%s cycle=%lu t_ms=%lu", fi_cur_exp_id, st,
                cur.fault->id, fault_class_str(cur.fault->cls), cur.fault->name, mech_name[cur.mech],
                (unsigned long)e->cycle, (unsigned long)e->t_ms);
            break;
        case FI_ST_ARMED:
            if (cur.mech == FI_MECH_TIMER) {
                LOG("FAULT", "EXP=%s state=%s mech=TIMER site=tim4_isr delay_ms=%lu target_cycle=%lu cycle=%lu t_ms=%lu",
                    fi_cur_exp_id, st, (unsigned long)e->a, (unsigned long)e->b, (unsigned long)e->cycle,
                    (unsigned long)e->t_ms);
            } else {
                LOG("FAULT", "EXP=%s state=%s mech=%s site=%s cycle=%lu t_ms=%lu", fi_cur_exp_id, st,
                    mech_name[cur.mech], cur.mech == FI_MECH_GDB ? "fi_gdb_anchor" : "control_cycle",
                    (unsigned long)e->cycle, (unsigned long)e->t_ms);
            }
            break;
        case FI_ST_INJECTED:
            if (cur.mech == FI_MECH_TIMER) {
                LOG("FAULT", "EXP=%s state=%s mech=TIMER target=%s before=0x%08lX after=0x%08lX inject_count=%lu "
                    "cycle=%lu t_ms=%lu target_cycle=%lu trigger_error_cycles=%ld",
                    fi_cur_exp_id, st, cur.fault->target, (unsigned long)e->a, (unsigned long)e->b,
                    (unsigned long)cur.inject_count, (unsigned long)e->cycle, (unsigned long)e->t_ms,
                    (unsigned long)cur.target_cycle, (long)(int32_t)(e->cycle - cur.target_cycle));
            } else {
                LOG("FAULT", "EXP=%s state=%s mech=%s target=%s before=0x%08lX after=0x%08lX inject_count=%lu "
                    "cycle=%lu t_ms=%lu",
                    fi_cur_exp_id, st, mech_name[cur.mech], cur.fault->target, (unsigned long)e->a,
                    (unsigned long)e->b, (unsigned long)cur.inject_count, (unsigned long)e->cycle,
                    (unsigned long)e->t_ms);
            }
            break;
        case FI_ST_OBSERVED:
            LOG("FAULT", "EXP=%s state=%s cycle=%lu t_ms=%lu cycles_since_injection=%lu", fi_cur_exp_id, st,
                (unsigned long)e->cycle, (unsigned long)e->t_ms, (unsigned long)(e->cycle - cur.inject_cycle));
            break;
        case FI_ST_COMPLETED:
            LOG("FAULT", "EXP=%s state=%s outcome=effect_observed inject_count=%lu cycle=%lu t_ms=%lu",
                fi_cur_exp_id, st, (unsigned long)e->a, (unsigned long)e->cycle, (unsigned long)e->t_ms);
            break;
        case FI_ST_ERROR:
            LOG("FAULT", "EXP=%s state=%s reason=%s inject_count=%lu cycle=%lu t_ms=%lu", fi_cur_exp_id, st,
                reason_name[e->reason], (unsigned long)e->a, (unsigned long)e->cycle, (unsigned long)e->t_ms);
            break;
        default:
            break;
        }
        cur.n_logged++;
    }
    fi_port_log_unlock(lock);
}

const char *fi_take_gdb_request(void)
{
    if (fi_gdb_req_magic != FI_GDB_REQ_MAGIC) {
        return NULL;
    }
    fi_gdb_req_magic = 0; /* consume exactly once */
    memcpy(gdb_req_copy, fi_gdb_req_id, sizeof gdb_req_copy);
    gdb_req_copy[sizeof gdb_req_copy - 1] = '\0';
    for (size_t i = 0; i < sizeof gdb_req_copy - 1 && gdb_req_copy[i] != '\0'; i++) {
        if (gdb_req_copy[i] < 0x20 || gdb_req_copy[i] > 0x7E) {
            gdb_req_copy[i] = '?'; /* keep the log printable; the parser rejects it */
        }
    }
    return gdb_req_copy;
}

const char *fi_active_exp(void)
{
    return (cur.fault != NULL && !is_terminal(cur.state)) ? fi_cur_exp_id : "none";
}
