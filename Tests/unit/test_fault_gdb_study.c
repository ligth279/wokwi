/* Host test of GDB-assisted injection of study faults (build `gdbtest`, -DFI_GDB_STUDY): which faults accept the GDB
 * mechanism, that a non-fatal fault is injected by the debugger and logged with before/after read from the target,
 * and that a fatal fault (CPU-01/02) is recorded and printed BEFORE the debugger acts, without calling inject(). */
#include "fault_fw.h"
#include "fault_inject.h"
#include "fault_study.h"
#include "fi_port.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void fi_host_reset(void);
extern void (*fi_host_gdb_hook)(void);

static uint32_t now_ms, now_cyc;
void fi_port_init(void) {}
int fi_port_log_lock(void) { return 0; }
void fi_port_log_unlock(int l) { (void)l; }
uint32_t fi_port_cycles(void) { return now_cyc; }
uint32_t fi_port_ms(void) { return now_ms; }
void fi_port_timer_start(uint32_t d) { (void)d; }
void fi_port_timer_stop(void) {}

static char lines[300][256];
static int nlines;
void host_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (nlines < 300) vsnprintf(lines[nlines++], sizeof lines[0], fmt, ap);
    va_end(ap);
}

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

static int count_lines(const char *a, const char *b)
{
    int n = 0;
    for (int i = 0; i < nlines; i++)
        if (strstr(lines[i], a) && (!b || strstr(lines[i], b))) n++;
    return n;
}
static int line_index(const char *a)
{
    for (int i = 0; i < nlines; i++)
        if (strstr(lines[i], a)) return i;
    return -1;
}

/* stubs: a variable the "debugger" modifies, and the fatal-fault plan values */
static uint32_t target_val = 0x898;
static int inject_calls, hook_calls, injected_line_before_hook;
static uint32_t read_stub(void) { return target_val; }
static void inject_stub(uint32_t *b, uint32_t *a) { inject_calls++; *b = 0; *a = 0; }
static void plan_stub(uint32_t *b, uint32_t *a) { *b = 0x08002A02u; *a = 0x28002A03u; }
static int observe_stub(void) { return target_val != 0x898; }
static void cleanup_stub(void) { target_val = 0x898; }
#define STUBS(n)                                                       \
    void fs_##n##_inject(uint32_t *b, uint32_t *a) { inject_stub(b, a); } \
    void fs_##n##_plan(uint32_t *b, uint32_t *a) { plan_stub(b, a); }     \
    int fs_##n##_observe(void) { return observe_stub(); }                 \
    void fs_##n##_cleanup(void) { cleanup_stub(); }                       \
    uint32_t fs_##n##_read(void) { return read_stub(); }
STUBS(mem01) STUBS(mem02) STUBS(cpu01) STUBS(cpu02) STUBS(tim01) STUBS(tim02)
STUBS(data01) STUBS(data02) STUBS(periph01)
int16_t fi_study_sensor_hook(int16_t c) { return c; }
void fi_study_site_sensor(void) {}

static void debugger_hook(void)
{
    hook_calls++;
    injected_line_before_hook = count_lines("state=INJECTED", "mech=GDB");
    target_val ^= 0x400u; /* what Tests/gdb/study_MEM-01.gdb does */
    fi_gdb_mailbox = FI_GDB_DONE;
}

static void run(void)
{
    for (int i = 0; i < 40; i++) {
        now_ms += 100;
        now_cyc += 7200000u;
        fi_site_control();
        fi_log_pending();
    }
}

int main(void)
{
    fi_host_gdb_hook = debugger_hook;

    printf("t_flags: only MEM-01, DATA-02, CPU-01, CPU-02 accept the GDB mechanism in this build\n");
    for (const char *id = "MEM-01"; id; id = NULL) {
        (void)id;
    }
    static const char *const ok[] = {"MEM-01", "DATA-02", "CPU-01", "CPU-02"};
    static const char *const no[] = {"MEM-02", "TIM-01", "TIM-02", "DATA-01", "PERIPH-01"};
    for (unsigned i = 0; i < 4; i++) CHECK(!(fault_find(ok[i])->flags & FAULT_F_NO_GDB), "%s GDB-capable", ok[i]);
    for (unsigned i = 0; i < 5; i++) CHECK(fault_find(no[i])->flags & FAULT_F_NO_GDB, "%s not GDB-capable", no[i]);

    printf("t_nonfatal: the debugger corrupts MEM-01, INJECTED reports before/after read from the target\n");
    nlines = 0; fi_host_reset(); target_val = 0x898; inject_calls = hook_calls = 0;
    CHECK(fi_select(fault_find("MEM-01"), FI_MECH_GDB, 0) == FI_SELECT_OK, "select");
    run();
    CHECK(hook_calls == 1, "debugger stopped at the anchor once (%d)", hook_calls);
    CHECK(inject_calls == 0, "the firmware's own inject() is not used by the GDB mechanism");
    CHECK(count_lines("EXP=MEM-01_001 state=INJECTED", "before=0x00000898 after=0x00000C98 inject_count=1") == 1, "INJECTED before/after read from the target");
    CHECK(count_lines("EXP=MEM-01_001 state=COMPLETED", NULL) == 1, "observed and completed");

    printf("t_fatal: CPU-01 is recorded and printed before the debugger acts; inject() is not called\n");
    for (unsigned i = 2; i < 4; i++) {
        nlines = 0; fi_host_reset(); inject_calls = hook_calls = injected_line_before_hook = 0;
        uint32_t total = g_faults_injected;
        CHECK(fi_select(fault_find(ok[i]), FI_MECH_GDB, 0) == FI_SELECT_OK, "select");
        run();
        CHECK(hook_calls == 1, "%s: debugger stopped once", ok[i]);
        CHECK(injected_line_before_hook == 1, "%s: INJECTED already printed when the debugger runs", ok[i]);
        CHECK(inject_calls == 0, "%s: inject() not called", ok[i]);
        CHECK(count_lines("state=INJECTED", "before=0x08002A02 after=0x28002A03") == 1, "%s: plan values logged", ok[i]);
        CHECK(g_faults_injected == total + 1, "%s: counted once", ok[i]);
        CHECK(line_index("state=INJECTED") >= 0 && count_lines("state=ERROR", NULL) == 0, "%s: no error", ok[i]);
    }

    printf("t_unsupported: the other study faults still refuse the GDB mechanism\n");
    for (unsigned i = 0; i < 5; i++) {
        nlines = 0; fi_host_reset(); inject_calls = hook_calls = 0;
        fi_select(fault_find(no[i]), FI_MECH_GDB, 0);
        run();
        CHECK(count_lines("state=ERROR", "reason=mechanism_unsupported") == 1 && hook_calls == 0 && inject_calls == 0, "%s refused", no[i]);
    }
    printf("[UNIT] test_fault_gdb_study checks=%d failures=%d\n", checks, failures);
    return failures != 0;
}
