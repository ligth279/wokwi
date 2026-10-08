/* Host test of how the framework drives the nine study faults
 * (Step 4): catalog wiring, the FATAL path (INJECTED is printed before the
 * injection routine runs), console-side observation (fi_poll), the GDB
 * restriction. The real injection routines (fault_study.c) touch hardware
 * and the RTOS and are exercised in Wokwi; here they are replaced by stubs
 * that record when they were called.
 *
 * Build + run: make unit */
#include "fault_fw.h"
#include "fault_inject.h"
#include "fault_study.h"
#include "fi_port.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CYC_PER_MS 72000u

void fi_host_reset(void);

/* ---- fake platform ---------------------------------------------------------- */
static uint32_t now_ms, now_cyc;
void fi_port_init(void) {}
int fi_port_log_lock(void) { return 0; }
void fi_port_log_unlock(int l) { (void)l; }
uint32_t fi_port_cycles(void) { return now_cyc; }
uint32_t fi_port_ms(void) { return now_ms; }
void fi_port_timer_start(uint32_t d) { (void)d; }
void fi_port_timer_stop(void) {}

#define MAXLINES 500
static char lines[MAXLINES][256];
static int nlines;
void host_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (nlines < MAXLINES) vsnprintf(lines[nlines++], sizeof lines[0], fmt, ap);
    va_end(ap);
}

/* ---- stubs for the injection routines ---------------------------------------- */
static int inject_calls, injected_line_seen_first, observe_calls, observe_result;
static int log_has(const char *a, const char *b);

static void stub_inject(uint32_t *b, uint32_t *a)
{
    inject_calls++;
    injected_line_seen_first = log_has("state=INJECTED", "before=0x00000011 after=0x00000022");
    *b = 0x11;
    *a = 0x22;
}
static void plan_stub(uint32_t *b, uint32_t *a) { *b = 0x11; *a = 0x22; }
static int observe_stub(void) { observe_calls++; return observe_result; }
static void cleanup_stub(void) {}
static uint32_t read_stub(void) { return 0; }

#define STUBS(n)                                                                  \
    void fs_##n##_inject(uint32_t *b, uint32_t *a) { stub_inject(b, a); }         \
    void fs_##n##_plan(uint32_t *b, uint32_t *a) { plan_stub(b, a); }             \
    int fs_##n##_observe(void) { return observe_stub(); }                         \
    void fs_##n##_cleanup(void) { cleanup_stub(); }                               \
    uint32_t fs_##n##_read(void) { return read_stub(); }
STUBS(mem01) STUBS(mem02) STUBS(cpu01) STUBS(cpu02) STUBS(tim01) STUBS(tim02)
STUBS(data01) STUBS(data02) STUBS(periph01)
int16_t fi_study_sensor_hook(int16_t c) { return c; }
void fi_study_site_sensor(void) {}

/* ---- helpers ------------------------------------------------------------------- */
static int failures, checks;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        checks++;                                         \
        if (!(cond)) {                                    \
            failures++;                                   \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

static int log_has(const char *a, const char *b)
{
    for (int i = 0; i < nlines; i++)
        if (strstr(lines[i], a) && (!b || strstr(lines[i], b))) return 1;
    return 0;
}
static int count_lines(const char *a, const char *b)
{
    int n = 0;
    for (int i = 0; i < nlines; i++)
        if (strstr(lines[i], a) && (!b || strstr(lines[i], b))) n++;
    return n;
}
static void reset_stubs(void)
{
    inject_calls = injected_line_seen_first = observe_calls = observe_result = 0;
    nlines = 0;
}
/* control cycle every 100 ms; console loop every 10 ms (poll + log flush) */
static void run_ms(uint32_t ms, int console_alive, int control_alive)
{
    for (uint32_t i = 0; i < ms; i++) {
        now_ms++;
        now_cyc += CYC_PER_MS;
        if (control_alive && now_ms % 100u == 0u) fi_site_control();
        if (console_alive && now_ms % 10u == 0u) {
            fi_poll();
            fi_log_pending();
        }
    }
}
/* a new simulated boot between experiments */
static void new_boot(void) { fi_host_reset(); reset_stubs(); }

static const char *const STUDY[] = {"MEM-01", "MEM-02", "CPU-01", "CPU-02", "TIM-01",
                                    "TIM-02", "DATA-01", "DATA-02", "PERIPH-01"};

static void t_catalog(void)
{
    printf("t_catalog: every study fault has a real injection routine\n");
    for (unsigned i = 0; i < sizeof STUDY / sizeof STUDY[0]; i++) {
        const fault_desc_t *f = fault_find(STUDY[i]);
        CHECK(f != NULL, "%s registered", STUDY[i]);
        if (!f) continue;
        CHECK(f->inject != NULL, "%s has inject", STUDY[i]);
        CHECK(f->flags & FAULT_F_NO_GDB, "%s is not GDB-injectable", STUDY[i]);
        if (f->flags & FAULT_F_FATAL) {
            CHECK(f->plan != NULL, "%s FATAL needs plan", STUDY[i]);
            CHECK(f->observe == NULL, "%s FATAL cannot observe itself", STUDY[i]);
        } else {
            CHECK(f->observe != NULL, "%s non-fatal needs observe", STUDY[i]);
        }
        if (f->flags & FAULT_F_OBS_CONSOLE) CHECK(f->observe != NULL, "%s console-observed needs observe", STUDY[i]);
    }
    unsigned fatal = 0;
    for (unsigned i = 0; i < sizeof STUDY / sizeof STUDY[0]; i++) fatal += (fault_find(STUDY[i])->flags & FAULT_F_FATAL) != 0;
    CHECK(fatal == 4, "FATAL set is MEM-02, CPU-01, CPU-02, TIM-01 (got %u)", fatal);
    CHECK(!(fault_find("FI-TEST")->flags & FAULT_F_NO_GDB), "FI-TEST keeps the GDB mechanism");
}

static void t_fatal(void)
{
    printf("t_fatal: INJECTED is printed before a FATAL routine runs, once\n");
    const char *fatal_ids[] = {"MEM-02", "CPU-01", "CPU-02", "TIM-01"};
    for (unsigned k = 0; k < 4; k++) {
        new_boot();
        uint32_t total = g_faults_injected;
        CHECK(fi_select(fault_find(fatal_ids[k]), FI_MECH_UART, 0) == FI_SELECT_OK, "select");
        run_ms(1, 1, 1);
        run_ms(5000, 1, 1); /* control keeps cycling in the host (it would not on target) */
        CHECK(inject_calls == 1, "%s: inject called %d times", fatal_ids[k], inject_calls);
        CHECK(injected_line_seen_first, "%s: INJECTED (with plan values) printed before inject()", fatal_ids[k]);
        CHECK(count_lines("state=INJECTED", "before=0x00000011 after=0x00000022 inject_count=1") == 1,
              "%s: one INJECTED line", fatal_ids[k]);
        CHECK(count_lines("state=OBSERVED", NULL) == 0 && count_lines("state=COMPLETED", NULL) == 0 &&
                  count_lines("state=ERROR", NULL) == 0,
              "%s: no terminal state is invented", fatal_ids[k]);
        CHECK(g_faults_injected == total + 1, "%s: faults_injected +1", fatal_ids[k]);
    }
    printf("t_fatal: injected from the timer interrupt path as well\n");
    new_boot();
    fi_select(fault_find("CPU-01"), FI_MECH_TIMER, 100);
    fi_timer_expired();
    CHECK(inject_calls == 1 && injected_line_seen_first, "timer path: planned, printed, injected");
}

static void t_nonfatal(void)
{
    printf("t_nonfatal: control-cycle observation, harness cleanup, exactly once\n");
    new_boot();
    fi_select(fault_find("MEM-01"), FI_MECH_UART, 0);
    observe_result = 0;
    run_ms(500, 1, 1);
    CHECK(inject_calls == 1, "injected once");
    CHECK(count_lines("state=COMPLETED", NULL) == 0, "not completed while the effect is not seen");
    observe_result = 1;
    run_ms(300, 1, 1);
    CHECK(count_lines("state=OBSERVED", NULL) == 1 && count_lines("state=COMPLETED", "inject_count=1") == 1,
          "OBSERVED then COMPLETED");
    CHECK(inject_calls == 1, "still one injection");
    new_boot();
    fi_select(fault_find("DATA-02"), FI_MECH_UART, 0);
    observe_result = 0;
    run_ms(2500, 1, 1);
    CHECK(count_lines("state=ERROR", "reason=effect_not_observed inject_count=1") == 1,
          "an effect that never shows ends in ERROR effect_not_observed");
}

static void t_console_observe(void)
{
    printf("t_console_observe: TIM-02 is observed by the console task, not the control cycle\n");
    new_boot();
    fi_select(fault_find("TIM-02"), FI_MECH_UART, 0);
    observe_result = 1;
    run_ms(100, 0, 1); /* console not running, control cycle runs the injection */
    CHECK(inject_calls == 1, "injected by the control cycle");
    run_ms(2000, 0, 1);
    CHECK(observe_calls == 0 && count_lines("state=OBSERVED", NULL) == 0,
          "the control cycle never observes a console-observed fault");
    run_ms(20, 1, 0); /* control stalled (as after TIM-02), console polls */
    CHECK(observe_calls >= 1 && count_lines("state=COMPLETED", NULL) == 1, "fi_poll observes and completes it");
    new_boot();
    fi_select(fault_find("TIM-02"), FI_MECH_UART, 0);
    observe_result = 0;
    run_ms(100, 0, 1);
    run_ms(1500, 1, 0);
    CHECK(count_lines("state=ERROR", "reason=effect_not_observed") == 1, "console-side observation window expires");
}

static void t_gdb(void)
{
    printf("t_gdb: study faults refuse the GDB mechanism without touching the system\n");
    for (unsigned i = 0; i < sizeof STUDY / sizeof STUDY[0]; i++) {
        new_boot();
        uint32_t total = g_faults_injected;
        fi_select(fault_find(STUDY[i]), FI_MECH_GDB, 0);
        run_ms(500, 1, 1);
        CHECK(count_lines("state=ERROR", "reason=mechanism_unsupported inject_count=0") == 1, "%s: ERROR", STUDY[i]);
        CHECK(count_lines("state=ARMED", NULL) == 0 && inject_calls == 0 && g_faults_injected == total,
              "%s: never armed or injected", STUDY[i]);
    }
}

int main(void)
{
    t_catalog();
    t_fatal();
    t_nonfatal();
    t_console_observe();
    t_gdb();
    printf("[UNIT] test_fault_study checks=%d failures=%d\n", checks, failures);
    return failures != 0;
}
