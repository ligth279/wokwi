/* Host test of the fault-injection framework (FaultInjection/Src/fault_fw.c)
 * with a fake clock, fake one-shot timer and a scripted "debugger".
 *
 * Covers Step 3.2 (experiment IDs), 3.3 (lifecycle, terminal states),
 * 3.4/3.5/3.6 framework side (UART, timer, GDB paths), 3.7 (cycle stamped at
 * injection) and 3.8 (exactly-once). It does NOT replace the Wokwi runs:
 * the real timer, UART and debugger are exercised there.
 *
 * Build + run: make unit   (add -v to see the captured log) */
#include "fault_fw.h"
#include "fault_inject.h"
#include "fi_port.h"
#include "fi_test.h"

#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CYC_PER_MS 72000u

void fi_host_reset(void);
extern void (*fi_host_gdb_hook)(void);

/* ---- fake platform --------------------------------------------------------- */

static uint32_t now_ms, now_cyc;
static int timer_armed, timer_enabled = 1;
static uint32_t timer_deadline_ms;

void fi_port_init(void) {}
int fi_port_log_lock(void) { return 0; }
void fi_port_log_unlock(int l) { (void)l; }
uint32_t fi_port_cycles(void) { return now_cyc; }
uint32_t fi_port_ms(void) { return now_ms; }
void fi_port_timer_start(uint32_t d) { timer_armed = 1; timer_deadline_ms = now_ms + d; }
void fi_port_timer_stop(void) { timer_armed = 0; }

/* ---- captured log ------------------------------------------------------------ */

#define MAXLINES 2000
static char lines[MAXLINES][256];
static int nlines, verbose;

void host_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (nlines < MAXLINES) {
        vsnprintf(lines[nlines], sizeof lines[0], fmt, ap);
        if (verbose) printf("    | %s\n", lines[nlines]);
        nlines++;
    }
    va_end(ap);
}

/* ---- test helpers ---------------------------------------------------------- */

static int failures, checks;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        checks++;                                           \
        if (!(cond)) {                                      \
            failures++;                                     \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

/* Advance time 1 ms at a time; fire the timer when due; run the control
 * cycle every 100 ms and the console's log flush every ms. */
static void run_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        now_ms++;
        now_cyc += CYC_PER_MS;
        if (timer_armed && timer_enabled && now_ms >= timer_deadline_ms) {
            timer_armed = 0;
            fi_timer_expired();
        }
        if (now_ms % 100u == 0u) {
            fi_site_control();
        }
        fi_log_pending();
    }
}

static int count_lines(const char *a, const char *b)
{
    int n = 0;
    for (int i = 0; i < nlines; i++) {
        if (strstr(lines[i], a) && (!b || strstr(lines[i], b))) n++;
    }
    return n;
}

static const char *find_line(const char *a, const char *b)
{
    for (int i = 0; i < nlines; i++) {
        if (strstr(lines[i], a) && (!b || strstr(lines[i], b))) return lines[i];
    }
    return NULL;
}

static unsigned long field(const char *line, const char *key)
{
    const char *p = line ? strstr(line, key) : NULL;
    return p ? strtoul(p + strlen(key), NULL, 0) : 0xFFFFFFFFul;
}

static const fault_desc_t *F(const char *id) { return fault_find(id); }

static uint32_t gdb_hits;
static void gdb_inject(void)
{
    gdb_hits++;
    fi_test_target ^= 1u; /* what Tests/gdb/fi_inject.gdb does */
    fi_gdb_mailbox = FI_GDB_DONE;
}

/* Lifecycle invariants over the whole captured log, per experiment:
 * order SELECTED < ARMED < INJECTED < OBSERVED < COMPLETED, or ERROR as the
 * last state; exactly one terminal state; at most one INJECTED. */
static void check_invariants(void)
{
    static const char *order[] = {"SELECTED", "ARMED", "INJECTED", "OBSERVED", "COMPLETED"};
    regex_t re;
    regcomp(&re, "EXP=([A-Z0-9-]+_[0-9]{3}) state=([A-Z]+)", REG_EXTENDED);
    char ids[64][24];
    int nids = 0;
    for (int i = 0; i < nlines; i++) {
        regmatch_t m[3];
        if (regexec(&re, lines[i], 3, m, 0) != 0) continue;
        char id[24];
        snprintf(id, sizeof id, "%.*s", (int)(m[1].rm_eo - m[1].rm_so), lines[i] + m[1].rm_so);
        int known = 0;
        for (int k = 0; k < nids; k++) known |= strcmp(ids[k], id) == 0;
        if (!known && nids < 64) strcpy(ids[nids++], id);
    }
    for (int k = 0; k < nids; k++) {
        int last = -1, injected = 0, terminal = 0, after_terminal = 0;
        char key[40];
        snprintf(key, sizeof key, "EXP=%s ", ids[k]);
        for (int i = 0; i < nlines; i++) {
            if (!strstr(lines[i], key)) continue;
            if (terminal) after_terminal = 1;
            const char *st = strstr(lines[i], "state=");
            if (st == NULL) continue;
            st += 6;
            if (!strncmp(st, "ERROR", 5)) { terminal = 1; continue; }
            int idx = -1;
            for (int o = 0; o < 5; o++) if (!strncmp(st, order[o], strlen(order[o]))) idx = o;
            CHECK(idx > last, "%s: state order violated at '%s'", ids[k], lines[i]);
            last = idx;
            if (idx == 2) injected++;
            if (idx == 4) terminal = 1;
        }
        CHECK(terminal, "%s never reached a terminal state", ids[k]);
        CHECK(!after_terminal, "%s logged events after its terminal state", ids[k]);
        CHECK(injected <= 1, "%s has %d INJECTED events", ids[k], injected);
    }
    regfree(&re);
}

/* ---- tests ---------------------------------------------------------------- */

static void t_uart(void)
{
    printf("t_uart: UART-triggered FI-TEST, lifecycle + exactly once\n");
    uint32_t before_total = g_faults_injected;
    CHECK(fi_select(F("FI-TEST"), FI_MECH_UART, 0) == FI_SELECT_OK, "select");
    fi_log_pending();
    CHECK(count_lines("EXP=FI-TEST_001 state=SELECTED", "mech=UART") == 1, "SELECTED logged");
    CHECK(count_lines("EXP=FI-TEST_001 state=ARMED", "site=control_cycle") == 1, "ARMED logged");
    CHECK(count_lines("FI-TEST_001 state=INJECTED", NULL) == 0, "not injected before the injection point");
    uint32_t arm_ms = now_ms;
    run_ms(1000); /* many control cycles and log polls */
    const char *inj = find_line("EXP=FI-TEST_001 state=INJECTED", NULL);
    CHECK(inj != NULL, "INJECTED logged");
    CHECK(count_lines("FI-TEST_001 state=INJECTED", NULL) == 1, "exactly one INJECTED");
    unsigned long t_inj = field(inj, "t_ms=");
    CHECK(t_inj == ((arm_ms / 100u) + 1u) * 100u, "injected at the next control cycle (t=%lu)", t_inj);
    CHECK(field(inj, "cycle=") == t_inj * CYC_PER_MS, "cycle stamped at injection");
    CHECK(inj && strstr(inj, "before=0xC0FFEE00 after=0xC0FFEE01") != NULL, "target flipped");
    CHECK(count_lines("EXP=FI-TEST_001 state=OBSERVED", NULL) == 1, "OBSERVED");
    CHECK(count_lines("EXP=FI-TEST_001 state=COMPLETED", "inject_count=1") == 1, "COMPLETED inject_count=1");
    CHECK(fi_test_target == FI_TEST_NOMINAL, "cleanup restored target");
    CHECK(g_faults_injected == before_total + 1, "faults_injected +1");
    CHECK(!strcmp(fi_active_exp(), "none"), "idle after completion");
}

static void t_sequence(void)
{
    printf("t_sequence: per-fault sequence numbers\n");
    for (int i = 0; i < 2; i++) {
        fi_select(F("FI-TEST"), FI_MECH_UART, 0);
        run_ms(400);
    }
    CHECK(count_lines("EXP=FI-TEST_002 state=COMPLETED", NULL) == 1, "FI-TEST_002");
    CHECK(count_lines("EXP=FI-TEST_003 state=COMPLETED", NULL) == 1, "FI-TEST_003");
    fi_select(F("MEM-01"), FI_MECH_UART, 0);
    run_ms(10);
    CHECK(count_lines("EXP=MEM-01_001 state=SELECTED", NULL) == 1, "independent sequence for MEM-01");
}

static void t_not_implemented(void)
{
    printf("t_not_implemented: study fault without injection routine\n");
    uint32_t total = g_faults_injected;
    fi_select(F("DATA-02"), FI_MECH_UART, 0);
    run_ms(500);
    CHECK(count_lines("EXP=DATA-02_001 state=ERROR", "reason=not_implemented inject_count=0") == 1, "ERROR");
    CHECK(count_lines("EXP=DATA-02_001 state=ARMED", NULL) == 0, "never armed");
    CHECK(g_faults_injected == total, "nothing injected");
}

static void t_busy(void)
{
    printf("t_busy: second request while an experiment runs\n");
    CHECK(fi_select(F("FI-TEST"), FI_MECH_TIMER, 1000) == FI_SELECT_OK, "timer select");
    run_ms(50);
    CHECK(fi_select(F("FI-TEST"), FI_MECH_UART, 0) == FI_SELECT_BUSY, "busy");
    CHECK(fi_select(F("MEM-02"), FI_MECH_UART, 0) == FI_SELECT_BUSY, "busy (other fault)");
    run_ms(1500);
    CHECK(count_lines("EXP=FI-TEST_004 state=COMPLETED", NULL) == 1, "timer experiment completed");
    CHECK(count_lines("EXP=FI-TEST_005", NULL) == 0, "busy request consumed no sequence number");
    CHECK(count_lines("EXP=MEM-02", NULL) == 0, "busy request created no experiment");
}

static void t_timer(void)
{
    printf("t_timer: timer-triggered injection, exactly once, timing\n");
    fi_select(F("FI-TEST"), FI_MECH_TIMER, 250);
    fi_log_pending();
    const char *arm = find_line("EXP=FI-TEST_005 state=ARMED", "mech=TIMER");
    CHECK(arm && field(arm, "delay_ms=") == 250, "ARMED delay_ms=250");
    unsigned long target = field(arm, "target_cycle=");
    CHECK(target == field(arm, " cycle=") + 250ul * CYC_PER_MS, "target_cycle = arm + 250 ms");
    run_ms(249);
    CHECK(count_lines("FI-TEST_005 state=INJECTED", NULL) == 0, "not before the trigger point");
    run_ms(1);
    fi_timer_expired(); /* a duplicate / spurious interrupt */
    fi_timer_expired();
    run_ms(500);
    const char *inj = find_line("EXP=FI-TEST_005 state=INJECTED", "mech=TIMER");
    CHECK(inj != NULL, "INJECTED");
    CHECK(count_lines("FI-TEST_005 state=INJECTED", NULL) == 1, "exactly one INJECTED despite extra IRQs");
    CHECK(field(inj, "cycle=") == target, "injected at target cycle");
    CHECK(inj && strstr(inj, "trigger_error_cycles=0") != NULL, "trigger error recorded");
    CHECK(count_lines("EXP=FI-TEST_005 state=COMPLETED", "inject_count=1") == 1, "COMPLETED");
}

static void t_timer_never_fires(void)
{
    printf("t_timer_never_fires: timer fault -> terminal ERROR\n");
    timer_enabled = 0;
    fi_select(F("FI-TEST"), FI_MECH_TIMER, 300);
    run_ms(2000);
    timer_enabled = 1;
    CHECK(count_lines("EXP=FI-TEST_006 state=ERROR", "reason=timer_not_fired inject_count=0") == 1, "ERROR");
    CHECK(fi_test_target == FI_TEST_NOMINAL, "target untouched");
}

static void t_gdb(void)
{
    printf("t_gdb: GDB-assisted injection at the anchor\n");
    fi_host_gdb_hook = gdb_inject;
    gdb_hits = 0;
    fi_select(F("FI-TEST"), FI_MECH_GDB, 0);
    fi_log_pending();
    CHECK(count_lines("EXP=FI-TEST_007 state=ARMED", "site=fi_gdb_anchor") == 1, "ARMED at anchor");
    run_ms(1000);
    fi_host_gdb_hook = NULL;
    CHECK(gdb_hits == 1, "debugger acted once (hits=%u)", gdb_hits);
    const char *inj = find_line("EXP=FI-TEST_007 state=INJECTED", "mech=GDB");
    CHECK(inj && strstr(inj, "before=0xC0FFEE00 after=0xC0FFEE01"), "GDB injection seen by framework");
    CHECK(inj && field(inj, "cycle=") == field(inj, "t_ms=") * CYC_PER_MS, "cycle = anchor cycle");
    CHECK(count_lines("FI-TEST_007 state=INJECTED", NULL) == 1, "exactly one INJECTED");
    CHECK(count_lines("EXP=FI-TEST_007 state=COMPLETED", NULL) == 1, "COMPLETED");
}

static void t_gdb_request(void)
{
    printf("t_gdb_request: request mailbox consumed exactly once\n");
    fi_gdb_req_magic = FI_GDB_REQ_MAGIC;
    strcpy(fi_gdb_req_id, "FI-TEST");
    const char *r1 = fi_take_gdb_request();
    const char *r2 = fi_take_gdb_request();
    CHECK(r1 && !strcmp(r1, "FI-TEST"), "request read");
    CHECK(r2 == NULL, "request consumed once");
    fi_gdb_req_magic = FI_GDB_REQ_MAGIC;
    memcpy(fi_gdb_req_id, "FI-\x01TEST", 9);
    const char *r3 = fi_take_gdb_request();
    CHECK(r3 && !strchr(r3, '\x01'), "non-printable sanitised for logging");
}

static void t_gdb_timeout(void)
{
    printf("t_gdb_timeout: armed for GDB, debugger never acts\n");
    fi_select(F("FI-TEST"), FI_MECH_GDB, 0);
    run_ms(11000);
    CHECK(count_lines("EXP=FI-TEST_008 state=ERROR", "reason=gdb_timeout inject_count=0") == 1, "ERROR");
}

static void t_not_observed(void)
{
    printf("t_not_observed: effect undone before observation -> ERROR\n");
    fi_select(F("FI-TEST"), FI_MECH_UART, 0);
    /* run until injected, then undo the effect behind the framework's back */
    for (int i = 0; i < 200 && !find_line("EXP=FI-TEST_009 state=INJECTED", NULL); i++) run_ms(1);
    fi_test_target = FI_TEST_NOMINAL;
    run_ms(1500);
    CHECK(count_lines("EXP=FI-TEST_009 state=ERROR", "reason=effect_not_observed inject_count=1") == 1, "ERROR");
}

static void t_reset(void)
{
    printf("t_reset: experiment interrupted by a system reset\n");
    fi_select(F("FI-TEST"), FI_MECH_TIMER, 2000);
    run_ms(100);
    fi_host_reset(); /* reset: RAM lost, .noinit kept */
    timer_armed = 0;
    fi_boot();
    CHECK(count_lines("EXP=FI-TEST_010 state=ERROR", "reason=interrupted_by_reset last_state=ARMED") == 1,
          "closed at boot");
    CHECK(count_lines("framework=ready persist=warm", NULL) == 1, "warm boot");
    fi_select(F("FI-TEST"), FI_MECH_UART, 0);
    run_ms(400);
    CHECK(count_lines("EXP=FI-TEST_011 state=COMPLETED", NULL) == 1, "sequence continues after reset");
    fi_host_reset();
    fi_boot();
    CHECK(count_lines("interrupted_by_reset", NULL) == 1, "completed experiment not reported again");
}

int main(int argc, char **argv)
{
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    fi_boot(); /* cold: .noinit is zero on the host */
    CHECK(count_lines("framework=ready persist=cold", NULL) == 1, "cold boot");

    t_uart();
    t_sequence();
    t_not_implemented();
    t_busy();
    t_timer();
    t_timer_never_fires();
    t_gdb();
    t_gdb_request();
    t_gdb_timeout();
    t_not_observed();
    t_reset();
    check_invariants();

    printf("[UNIT] test_fault_fw checks=%d failures=%d log_lines=%d\n", checks, failures, nlines);
    return failures != 0;
}
