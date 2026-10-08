#include "recovery.h"

#if RECOVERY

#include "app.h"
#include "board.h"
#include "dwt.h"
#include "fault_fw.h"
#include "i2c_recovery.h"
#include "log.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stddef.h>
#include <string.h>

#define CYC_PER_MS      72000u
#define VERIFY_MS_L1    1500u   /* restart / bus recovery must show normal operation within this */
#define VERIFY_MS_L2    1000u
#define VERIFY_MS_L3    5000u   /* after a reset */
#define CHECKPOINT_MS   500u

/* ---- persistent state (.noinit: survives resets, not power-on) --------------- */

#define REC_MAGIC 0x5EC0FE12u
typedef struct {
    uint8_t  valid;
    uint8_t  action;
    uint8_t  attempt;
    uint8_t  mech;
    int8_t   task;
    char     exp[24];
    uint32_t det_cycle, inj_cycle, start_cycle, reset_cycle;
} rec_pending_t;

typedef struct {
    uint32_t         magic;
    uint32_t         consecutive;   /* attempts since the last healthy window */
    uint32_t         total_attempts;
    uint8_t          safe;
    char             safe_reason[32];
    char             safe_exp[24];
    uint32_t         safe_attempts;
    rec_pending_t    pend;         /* a recovery that needs the next boot to complete */
    char             ep_exp[24];   /* experiment of the unresolved episode (association across resets) */
    uint32_t         ep_inj_cycle;
    rec_checkpoint_t cp;
    uint32_t         check;
} rec_persist_t;

static rec_persist_t P __attribute__((section(".noinit")));

static uint32_t psum(const rec_persist_t *p)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t s = 0x7E5Eu;
    for (size_t i = 0; i < offsetof(rec_persist_t, check); i++) {
        s = s * 31u + b[i];
    }
    return s;
}
static void psave(void) { P.check = psum(&P); }

/* ---- the running attempt -------------------------------------------------------- */

typedef struct {
    volatile uint8_t active;
    uint8_t          action;
    uint8_t          attempt;
    uint8_t          mech;
    int8_t           task;
    uint8_t          executed;     /* the action has been carried out, now verifying */
    uint8_t          post_reset;   /* verification happens after a reset */
    char             exp[24];
    uint32_t         det_cycle, inj_cycle, start_cycle;
    uint32_t         deadline_ms, t0_ms;
    uint32_t         sensor_hb0, control_hb0, console_hb0, err0;
    uint8_t          l2_good;      /* good cycles seen since the L2 action */
} attempt_t;

static attempt_t A;
static volatile uint8_t  req_pending;       /* a request is waiting for its executor */
static attempt_t         R;                 /* the request */
static volatile int16_t  good_input;
static volatile uint8_t  have_good_input;
static uint32_t          last_event_ms;
static uint32_t          cp_last_ms;
static uint8_t           healthy_cleared;
static uint32_t          boot_ms0;
static volatile uint32_t stat_attempts[5], stat_success[5];

static const char *const task_names[] = {"sensor", "control", "console", "monitor"};
static const char *tname(int t) { return (t >= 0 && t < 4) ? task_names[t] : "none"; }

static void copy_exp(char *dst, const char *src)
{
    strncpy(dst, src, 23);
    dst[23] = '\0';
}

/* ---- logging ----------------------------------------------------------------------- */

static void log_start(const attempt_t *a, const char *from)
{
    LOG("RECOVERY",
        "EXP=%s attempt=%u level=%d action=%s mech=%s task=%s state=START start_cycle=%lu det_cycle=%lu inj_cycle=%lu "
        "consecutive=%lu t_ms=%lu%s%s",
        a->exp, a->attempt, rec_level((rec_action_t)a->action), rec_action_name((rec_action_t)a->action),
        det_mech_name((det_mech_t)a->mech), tname(a->task), (unsigned long)a->start_cycle, (unsigned long)a->det_cycle,
        (unsigned long)a->inj_cycle, (unsigned long)P.consecutive, (unsigned long)HAL_GetTick(), from ? " escalated_from=" : "",
        from ? from : "");
}

static void log_done(const attempt_t *a, int ok, uint32_t end_cycle, const char *what, const char *escalate)
{
    uint32_t dt = end_cycle - a->start_cycle;
    int level = rec_level((rec_action_t)a->action);
    if (level >= 1 && level <= 4) {
        stat_attempts[level]++;
        stat_success[level] += ok ? 1u : 0u;
    }
    if (ok) {
        LOG("RECOVERY", "EXP=%s attempt=%u level=%d action=%s state=COMPLETE success=1 start_cycle=%lu end_cycle=%lu time_cycles=%lu verified=%s t_ms=%lu",
            a->exp, a->attempt, level, rec_action_name((rec_action_t)a->action), (unsigned long)a->start_cycle,
            (unsigned long)end_cycle, (unsigned long)dt, what, (unsigned long)HAL_GetTick());
    } else {
        LOG("RECOVERY", "EXP=%s attempt=%u level=%d action=%s state=FAILED success=0 start_cycle=%lu end_cycle=%lu time_cycles=none reason=%s escalate_to=%s t_ms=%lu",
            a->exp, a->attempt, level, rec_action_name((rec_action_t)a->action), (unsigned long)a->start_cycle,
            (unsigned long)end_cycle, what, escalate, (unsigned long)HAL_GetTick());
    }
}

/* ---- checkpoint ------------------------------------------------------------------- */

static void checkpoint_save(void)
{
    rec_checkpoint_t cp;
    memset(&cp, 0, sizeof cp);
    memcpy(cp.cfg, &g_config, sizeof cp.cfg);
    cp.last_input = have_good_input ? good_input : g_state.temp_centi;
    cp.seq = P.cp.seq + 1u;
    rec_cp_seal(&cp);
    P.cp = cp;
    psave();
}

/* ---- boot --------------------------------------------------------------------------- */

void rec_boot(uint32_t csr)
{
    (void)csr;
    boot_ms0 = HAL_GetTick();
    int warm = P.magic == REC_MAGIC && P.check == psum(&P);
    if (!warm) {
        memset(&P, 0, sizeof P);
        P.magic = REC_MAGIC;
        psave();
        LOG("RECOVERY", "boot_path=normal_boot persist=cold");
        return;
    }
    const char *path = P.safe ? "safe_state" : P.pend.valid ? "verify_recovery" : "normal_boot";
    LOG("RECOVERY", "boot_path=%s persist=warm csr=0x%08lX consecutive=%lu pending_exp=%s pending_level=%d pending_action=%s safe=%u", path,
        (unsigned long)csr, (unsigned long)P.consecutive, P.pend.valid ? P.pend.exp : "none",
        P.pend.valid ? rec_level((rec_action_t)P.pend.action) : 0, P.pend.valid ? rec_action_name((rec_action_t)P.pend.action) : "none",
        P.safe);
    /* state restore (6.3): the config comes back from the checkpoint taken before the reset */
    if (!P.safe && rec_cp_valid(&P.cp)) {
        memcpy(&g_config, P.cp.cfg, sizeof P.cp.cfg);
        have_good_input = 1;
        good_input = P.cp.last_input;
        LOG("RECOVERY", "state_restored from=checkpoint cp_seq=%lu setpoint=%d kp=%d out_min=%d out_max=%d last_input=%d",
            (unsigned long)P.cp.seq, P.cp.cfg[0], P.cp.cfg[1], P.cp.cfg[2], P.cp.cfg[3], P.cp.last_input);
    }
    if (P.pend.valid) {
        /* the attempt continues here: verify normal operation once the tasks run */
        memset(&A, 0, sizeof A);
        A.active = 1;
        A.post_reset = 1;
        A.action = P.pend.action;
        A.attempt = P.pend.attempt;
        A.mech = P.pend.mech;
        A.task = P.pend.task;
        copy_exp(A.exp, P.pend.exp);
        A.det_cycle = P.pend.det_cycle;
        A.inj_cycle = P.pend.inj_cycle;
        A.start_cycle = P.pend.start_cycle;
        A.executed = 1;
        A.t0_ms = HAL_GetTick();
        A.deadline_ms = A.t0_ms + VERIFY_MS_L3;
    }
}

const char *rec_episode_exp(uint32_t *inj_cycle)
{
    if (P.magic == REC_MAGIC && P.ep_exp[0] != '\0') {
        *inj_cycle = P.ep_inj_cycle;
        return P.ep_exp;
    }
    return NULL;
}

int rec_safe_mode(void) { return P.magic == REC_MAGIC && P.safe; }
int rec_active(void) { return A.active; }

/* ---- safe state ------------------------------------------------------------------------ */

static void enter_safe(const char *reason, const char *exp, uint32_t det_cycle, uint32_t inj_cycle, int reset_now)
{
    uint32_t now = dwt_cycles();
    attempt_t a;
    memset(&a, 0, sizeof a);
    a.action = REC_ACT_SAFE_STATE;
    a.attempt = (uint8_t)(P.total_attempts + 1u);
    a.task = -1;
    a.det_cycle = det_cycle;
    a.inj_cycle = inj_cycle;
    a.start_cycle = now;
    copy_exp(a.exp, exp);
    P.total_attempts++;
    P.safe = 1;
    copy_exp(P.safe_reason, reason);
    copy_exp(P.safe_exp, exp);
    P.safe_attempts = P.total_attempts;
    P.pend.valid = 0;
    /* the safe state is entered through a reset: a clean boot without the faulty application */
    P.pend.valid = 1;
    P.pend.action = REC_ACT_SAFE_STATE;
    P.pend.attempt = a.attempt;
    P.pend.mech = A.mech;
    P.pend.task = -1;
    copy_exp(P.pend.exp, exp);
    P.pend.det_cycle = det_cycle;
    P.pend.inj_cycle = inj_cycle;
    P.pend.start_cycle = now;
    psave();
    log_start(&a, NULL);
    LOG("SAFE", "state=ENTERING reason=%s exp=%s consecutive=%lu action=reset_into_safe_state", reason, exp, (unsigned long)P.consecutive);
    if (reset_now) {
        det_crumb_save(DET_CRUMB_SOFT, dwt_cycles());
        NVIC_SystemReset();
        for (;;) {
        }
    }
}

void rec_safe_main(void)
{
    extern void rec_safe_task(void *arg);
    LOG("SAFE", "state=ENTERED reason=%s exp=%s level=4 output=%d attempts=%lu escalation_to_level_4=1", P.safe_reason, P.safe_exp,
        (int)g_config.out_max, (unsigned long)P.safe_attempts);
    log_rtos_init();
    xTaskCreate(rec_safe_task, "safe", 320, NULL, 1, NULL);
    vTaskStartScheduler();
    for (;;) {
    }
}

void rec_safe_task(void *arg)
{
    (void)arg;
    uint32_t n = 0;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
        n++;
        LOG("SAFE", "state=HOLD output=%d exp=%s t_ms=%lu hold_count=%lu normal_operation=0", (int)g_config.out_max, P.safe_exp,
            (unsigned long)HAL_GetTick(), (unsigned long)n);
        if (n == 2 && P.pend.valid) {
            /* the safe state has stayed stable for two periods: that attempt (level 4) is verified */
            attempt_t a;
            memset(&a, 0, sizeof a);
            a.action = REC_ACT_SAFE_STATE;
            a.attempt = P.pend.attempt;
            a.start_cycle = P.pend.start_cycle;
            copy_exp(a.exp, P.pend.exp);
            log_done(&a, 1, dwt_cycles(), "safe_state_stable_2s", "none");
            P.pend.valid = 0;
            psave();
        }
    }
}

/* ---- starting an attempt ------------------------------------------------------------------ */

static void begin_attempt(rec_action_t act, const char *exp, det_mech_t m, int task, uint32_t det_cycle, uint32_t inj_cycle,
                          const char *escalated_from)
{
    memset(&R, 0, sizeof R);
    R.action = (uint8_t)act;
    R.mech = (uint8_t)m;
    R.task = (int8_t)task;
    R.det_cycle = det_cycle;
    R.inj_cycle = inj_cycle;
    copy_exp(R.exp, exp);
    P.consecutive++;
    P.total_attempts++;
    R.attempt = (uint8_t)P.total_attempts;
    copy_exp(P.ep_exp, exp);
    P.ep_inj_cycle = inj_cycle;
    psave();
    last_event_ms = HAL_GetTick();
    healthy_cleared = 0;
    (void)escalated_from;
    req_pending = 1; /* the executor (control task for L2, monitor for the rest) starts it */
}

static void start_exec(attempt_t *a, const char *from)
{
    a->start_cycle = dwt_cycles();
    a->t0_ms = HAL_GetTick();
    a->sensor_hb0 = g_state.sensor_hb;
    a->control_hb0 = g_state.control_hb;
    a->console_hb0 = g_state.console_hb;
    a->err0 = g_state.sensor_errors;
    a->active = 1;
    log_start(a, from);
}

void rec_on_detect(det_mech_t m, int task, int sample_fault, const char *exp, uint32_t det_cycle, uint32_t inj_cycle)
{
    if (P.safe || m == DET_M_WWDG) {
        return; /* WWDG is decided in the ISR (rec_wwdg_begin) */
    }
    rec_action_t act = rec_select(m, task, sample_fault);
    if (act == REC_ACT_NONE) {
        return;
    }
    if (A.active) {
        if (!A.post_reset) {
            return; /* duplicate detection of the fault being recovered */
        }
        /* the fault is back while the recovery after the reset was still being verified: that recovery failed */
        log_done(&A, 0, dwt_cycles(), "fault_repeated_before_verified", "next_level");
        P.pend.valid = 0;
        A.active = 0;
    }
    if (req_pending) {
        return; /* second mechanism for the same fault (e.g. CRC and redundant copy) */
    }
    if (!rec_may_attempt(P.consecutive)) {
        enter_safe("repeated_faults", exp, det_cycle, inj_cycle, 1);
        return;
    }
    begin_attempt(act, exp, m, task, det_cycle, inj_cycle, NULL);
}

int rec_wwdg_begin(const char *exp, uint32_t det_cycle, uint32_t inj_cycle)
{
    uint32_t now = dwt_cycles();
    if (A.active && A.post_reset) {
        log_done(&A, 0, now, "fault_repeated_before_verified", "next_level");
        A.active = 0;
        P.pend.valid = 0;
    }
    if (!rec_may_attempt(P.consecutive)) {
        enter_safe("repeated_faults", exp, det_cycle, inj_cycle, 0);
        return 1; /* the ISR resets; the next boot enters the safe state */
    }
    attempt_t a;
    memset(&a, 0, sizeof a);
    a.action = REC_ACT_WWDG_RESET;
    a.mech = DET_M_WWDG;
    a.task = -1;
    a.det_cycle = det_cycle;
    a.inj_cycle = inj_cycle;
    copy_exp(a.exp, exp);
    P.consecutive++;
    P.total_attempts++;
    a.attempt = (uint8_t)P.total_attempts;
    a.start_cycle = now;
    copy_exp(P.ep_exp, exp);
    P.ep_inj_cycle = inj_cycle;
    P.pend.valid = 1;
    P.pend.action = a.action;
    P.pend.attempt = a.attempt;
    P.pend.mech = a.mech;
    P.pend.task = -1;
    copy_exp(P.pend.exp, exp);
    P.pend.det_cycle = det_cycle;
    P.pend.inj_cycle = inj_cycle;
    P.pend.start_cycle = a.start_cycle;
    psave();
    log_start(&a, NULL);
    return 1;
}

/* ---- executors --------------------------------------------------------------------------------- */

static void do_reset(attempt_t *a)
{
    P.pend.valid = 1;
    P.pend.action = a->action;
    P.pend.attempt = a->attempt;
    P.pend.mech = a->mech;
    P.pend.task = a->task;
    copy_exp(P.pend.exp, a->exp);
    P.pend.det_cycle = a->det_cycle;
    P.pend.inj_cycle = a->inj_cycle;
    P.pend.start_cycle = a->start_cycle;
    P.pend.reset_cycle = dwt_cycles();
    psave();
    det_crumb_save(DET_CRUMB_SOFT, dwt_cycles());
    NVIC_SystemReset();
    for (;;) {
    }
}

static void fail_attempt(const char *reason)
{
    uint32_t now = dwt_cycles();
    rec_action_t next = rec_escalate((rec_action_t)A.action);
    attempt_t failed = A;
    A.active = 0;
    int gate = rec_may_attempt(P.consecutive);
    log_done(&failed, 0, now, reason, gate ? rec_action_name(next) : "safe_state");
    if (!gate || next == REC_ACT_SAFE_STATE) {
        enter_safe("repeated_recovery_failure", failed.exp, failed.det_cycle, failed.inj_cycle, 1);
        return;
    }
    memset(&R, 0, sizeof R);
    R.action = (uint8_t)next;
    R.mech = failed.mech;
    R.task = failed.task;
    R.det_cycle = failed.det_cycle;
    R.inj_cycle = failed.inj_cycle;
    copy_exp(R.exp, failed.exp);
    P.consecutive++;
    P.total_attempts++;
    R.attempt = (uint8_t)P.total_attempts;
    psave();
    req_pending = 1;
}

static void exec_request(int from_control)
{
    if (!req_pending) {
        return;
    }
    rec_action_t act = (rec_action_t)R.action;
    int l2 = rec_level(act) == 2;
    if (l2 != from_control) {
        return;
    }
    req_pending = 0;
    A = R;
    start_exec(&A, NULL);
    switch (act) {
    case REC_ACT_RESTART_SENSOR:
        vTaskSuspendAll(); /* the new task must not run before its guards are rebuilt */
        app_restart_task(0);
        det_task_rebind(DET_TASK_SENSOR);
        xTaskResumeAll();
        A.executed = 1;
        A.deadline_ms = A.t0_ms + VERIFY_MS_L1;
        break;
    case REC_ACT_RESTART_CONTROL:
        vTaskSuspendAll(); /* the new task must not run before its guards are rebuilt */
        app_restart_task(1);
        det_task_rebind(DET_TASK_CONTROL);
        xTaskResumeAll();
        A.executed = 1;
        A.deadline_ms = A.t0_ms + VERIFY_MS_L1;
        break;
    case REC_ACT_RESTART_CONSOLE:
        vTaskSuspendAll(); /* the new task must not run before its guards are rebuilt */
        app_restart_task(2);
        det_task_rebind(DET_TASK_CONSOLE);
        xTaskResumeAll();
        A.executed = 1;
        A.deadline_ms = A.t0_ms + VERIFY_MS_L1;
        break;
    case REC_ACT_BUS_RECOVER: {
        /* the sensor task must not use the bus while it is cleared */
        vTaskSuspend((TaskHandle_t)g_task_sensor);
        i2c_recovery_report_t rep;
        int ok = i2c_bus_recover(&rep);
        LOG("RECOVERY", "EXP=%s attempt=%u action=i2c_bus_recovery sda_low_before=%d clocks=%lu sda_high_after=%d reinit=%d ok=%d", A.exp, A.attempt,
            rep.sda_low_before, (unsigned long)rep.clocks_sent, rep.sda_high_after, rep.reinit_ok, ok);
        vTaskResume((TaskHandle_t)g_task_sensor);
        A.executed = 1;
        A.deadline_ms = A.t0_ms + VERIFY_MS_L1;
        A.sensor_hb0 = g_state.sensor_hb;
        A.err0 = g_state.sensor_errors;
        break;
    }
    case REC_ACT_RESTORE_CONFIG: {
        int ok = rec_cp_valid(&P.cp);
        if (ok) {
            memcpy(&g_config, P.cp.cfg, sizeof P.cp.cfg);
        }
        LOG("RECOVERY", "EXP=%s attempt=%u action=config_restore checkpoint_valid=%d cp_seq=%lu restored_setpoint=%d restored_kp=%d", A.exp, A.attempt, ok,
            (unsigned long)P.cp.seq, P.cp.cfg[0], P.cp.cfg[1]);
        A.executed = 1;
        A.deadline_ms = A.t0_ms + VERIFY_MS_L2;
        if (!ok) {
            fail_attempt("checkpoint_invalid");
        }
        break;
    }
    case REC_ACT_RESTORE_SAMPLE:
        A.executed = 1; /* the control task substitutes the checkpointed input (rec_inline) */
        A.deadline_ms = A.t0_ms + VERIFY_MS_L2;
        break;
    case REC_ACT_SOFT_RESET:
        do_reset(&A);
        break;
    default:
        A.active = 0;
        break;
    }
}

/* Control task, after the detection checks of a cycle. */
int rec_inline(int16_t *input_centi, int16_t sensor_centi)
{
    (void)sensor_centi;
    int replaced = 0;
    exec_request(1);
    if (A.active && A.executed && (rec_action_t)A.action == REC_ACT_RESTORE_SAMPLE && A.l2_good == 0 && rec_cp_valid(&P.cp)) {
        LOG("RECOVERY", "EXP=%s attempt=%u action=sample_restore corrupted_input=%d restored_input=%d cp_seq=%lu", A.exp, A.attempt, *input_centi,
            P.cp.last_input, (unsigned long)P.cp.seq);
        *input_centi = P.cp.last_input;
        replaced = 1;
    }
    return replaced;
}

void rec_note_input(int16_t centi)
{
    good_input = centi;
    have_good_input = 1;
}

/* Control task, end of a cycle: verification of the level 2 actions. */
static int16_t law(int16_t input, const int16_t *c)
{
    int32_t out = (((int32_t)input - c[0]) * c[1]) / 100;
    if (out < c[2]) out = c[2];
    if (out > c[3]) out = c[3];
    return (int16_t)out;
}

void rec_control_done(int16_t input, int16_t output, int cfg_ok)
{
    if (!A.active || !A.executed || rec_level((rec_action_t)A.action) != 2) {
        return;
    }
    int good = cfg_ok;
    if ((rec_action_t)A.action == REC_ACT_RESTORE_CONFIG) {
        good = cfg_ok && output == law(input, P.cp.cfg); /* the output follows the checkpointed configuration */
    }
    if (!good) {
        A.l2_good = 0;
        return;
    }
    if ((rec_action_t)A.action == REC_ACT_RESTORE_SAMPLE && A.l2_good == 0) {
        A.l2_good = 1; /* the cycle that used the substituted input; verify on the next ones */
        return;
    }
    if (++A.l2_good >= 3) {
        log_done(&A, 1, dwt_cycles(),
                 (rec_action_t)A.action == REC_ACT_RESTORE_CONFIG ? "config_ok_and_output_matches_checkpoint" : "input_normal_3_cycles", "none");
        A.active = 0;
    }
}

/* ---- monitor ------------------------------------------------------------------------------- */

void rec_step(uint32_t now_ms)
{
    exec_request(0);

    if (A.active && A.executed) {
        int lvl = rec_level((rec_action_t)A.action);
        if (lvl == 1) {
            int ok;
            if ((rec_action_t)A.action == REC_ACT_BUS_RECOVER) {
                ok = g_state.sensor_hb >= A.sensor_hb0 + 3u && g_state.sensor_errors == A.err0;
            } else {
                ok = g_state.sensor_hb >= A.sensor_hb0 + 3u && g_state.control_hb >= A.control_hb0 + 3u &&
                     g_state.console_hb >= A.console_hb0 + 3u && g_state.sensor_errors == A.err0;
            }
            if (ok) {
                log_done(&A, 1, dwt_cycles(), (rec_action_t)A.action == REC_ACT_BUS_RECOVER ? "sensor_reads_ok" : "heartbeats_advancing_and_sensor_ok", "none");
                A.active = 0;
            } else if ((int32_t)(now_ms - A.deadline_ms) > 0) {
                fail_attempt("no_normal_operation_within_timeout");
            }
        } else if (lvl == 2) {
            if ((int32_t)(now_ms - A.deadline_ms) > 0) {
                fail_attempt("no_normal_operation_within_timeout");
            }
        } else if (lvl == 3 && A.post_reset) {
            int ok = g_state.sensor_hb >= 3u && g_state.control_hb >= 3u && g_state.sensor_errors == 0u;
            if (ok) {
                log_done(&A, 1, dwt_cycles(), "tasks_running_after_reset", "none");
                P.pend.valid = 0;
                psave();
                A.active = 0;
            } else if ((int32_t)(now_ms - A.deadline_ms) > 0) {
                P.pend.valid = 0;
                fail_attempt("no_normal_operation_after_reset");
            }
        }
    }

    /* healthy window: an episode ends after REC_HEALTHY_MS without recovery activity */
    if (!A.active && !req_pending && !P.safe) {
        if (!healthy_cleared && (now_ms - last_event_ms) >= REC_HEALTHY_MS && (now_ms - boot_ms0) >= REC_HEALTHY_MS) {
            if (P.consecutive != 0u || P.ep_exp[0] != '\0') {
                LOG("RECOVERY", "episode_end healthy_ms=%lu consecutive_was=%lu", (unsigned long)(now_ms - last_event_ms), (unsigned long)P.consecutive);
            }
            P.consecutive = 0;
            P.ep_exp[0] = '\0';
            psave();
            healthy_cleared = 1;
        }
        if ((now_ms - cp_last_ms) >= CHECKPOINT_MS && det_config_ok()) {
            checkpoint_save();
            cp_last_ms = now_ms;
        }
    }
}

#endif /* RECOVERY */
