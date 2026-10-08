/* Detection framework: one report path for every mechanism, the reset
 * breadcrumb, and the boot-time bring-up. */
#include "detect.h"

#if PROTECTED

#include "app.h"
#include "board.h"
#include "dwt.h"
#include "fault_fw.h"
#if RECOVERY
#include "recovery.h"
#endif
#include "log.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

det_last_t det_last;
volatile uint32_t det_count_total, det_count_false, det_count_suppressed;

static uint32_t seen_serial[DET_M_COUNT];
static uint8_t  seen[DET_M_COUNT];

static const char *const mech_names[DET_M_COUNT] = {"NONE", "WWDG", "FAULT_HANDLER", "STACK_CANARY", "STACK_SEAL",
                                                    "STACK_PAINT", "CRC", "REDUNDANT", "HEARTBEAT", "I2C_TIMEOUT"};

const char *det_mech_name(det_mech_t m)
{
    return (unsigned)m < DET_M_COUNT ? mech_names[m] : "?";
}

#if RECOVERY
static void report_v(det_mech_t m, int task, const char *fmt, va_list ap)
{
    uint32_t cyc = dwt_cycles(); /* the moment the check found the fault */
    const char *exp;
    uint32_t inj;
    uint32_t serial = fi_last_injection(&exp, &inj);
    if (serial == 0u) {
        /* the injection happened before a reset that is part of the unresolved recovery episode */
        uint32_t ep_inj;
        const char *ep = rec_episode_exp(&ep_inj);
        if (ep != NULL) {
            exp = ep;
            inj = ep_inj;
            serial = 0xE0000000u;
        }
    }

    if (m <= DET_M_NONE || m >= DET_M_COUNT) {
        return;
    }
    if (seen[m] && seen_serial[m] == serial) {
        det_count_suppressed++;
        return;
    }
    seen[m] = 1;
    seen_serial[m] = serial;

    char d[128];
    vsnprintf(d, sizeof d, fmt, ap);

    strncpy(det_last.exp, exp, sizeof det_last.exp - 1);
    det_last.exp[sizeof det_last.exp - 1] = '\0';
    det_last.det_cycle = cyc;
    det_last.inj_cycle = inj;
    det_last.latency = serial ? cyc - inj : 0;
    det_last.serial = serial;
    det_last.mech = (uint8_t)m;
    det_count_total++;

    if (serial) {
        LOG("DETECT", "EXP=%s mech=%s det_cycle=%lu inj_cycle=%lu latency_cycles=%lu t_ms=%lu %s", exp, det_mech_name(m),
            (unsigned long)cyc, (unsigned long)inj, (unsigned long)det_last.latency, (unsigned long)HAL_GetTick(), d);
    } else {
        det_count_false++;
        LOG("DETECT", "EXP=none mech=%s det_cycle=%lu false_positive=1 t_ms=%lu %s", det_mech_name(m), (unsigned long)cyc,
            (unsigned long)HAL_GetTick(), d);
    }
    rec_on_detect(m, task, strstr(d, "what=sample") != NULL, serial ? exp : "none", cyc, inj);
}

void det_report(det_mech_t m, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    report_v(m, -1, fmt, ap);
    va_end(ap);
}

void det_report_task(det_mech_t m, int task, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    report_v(m, task, fmt, ap);
    va_end(ap);
}
#else
/* Report one detection. Each mechanism reports at most once per injected
 * experiment (so a persisting corruption does not flood the log); a report
 * with no injected experiment is a false positive and is logged as such.
 * Safe from tasks and ISRs (the logger writes directly in handler mode). */
void det_report(det_mech_t m, const char *fmt, ...)
{
    uint32_t cyc = dwt_cycles(); /* the moment the check found the fault */
    const char *exp;
    uint32_t inj;
    uint32_t serial = fi_last_injection(&exp, &inj);

    if (m <= DET_M_NONE || m >= DET_M_COUNT) {
        return;
    }
    if (seen[m] && seen_serial[m] == serial) {
        det_count_suppressed++;
        return;
    }
    seen[m] = 1;
    seen_serial[m] = serial;

    char d[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d, sizeof d, fmt, ap);
    va_end(ap);

    strncpy(det_last.exp, exp, sizeof det_last.exp - 1);
    det_last.exp[sizeof det_last.exp - 1] = '\0';
    det_last.det_cycle = cyc;
    det_last.inj_cycle = inj;
    det_last.latency = serial ? cyc - inj : 0;
    det_last.serial = serial;
    det_last.mech = (uint8_t)m;
    det_count_total++;

    if (serial) {
        LOG("DETECT", "EXP=%s mech=%s det_cycle=%lu inj_cycle=%lu latency_cycles=%lu t_ms=%lu %s", exp,
            det_mech_name(m), (unsigned long)cyc, (unsigned long)inj, (unsigned long)det_last.latency,
            (unsigned long)HAL_GetTick(), d);
    } else {
        det_count_false++;
        LOG("DETECT", "EXP=none mech=%s det_cycle=%lu false_positive=1 t_ms=%lu %s", det_mech_name(m),
            (unsigned long)cyc, (unsigned long)HAL_GetTick(), d);
    }
}

#endif /* RECOVERY */

/* ---- reset breadcrumb ---------------------------------------------------- */

#define CRUMB_MAGIC 0xDE7EC7EDu
typedef struct {
    uint32_t magic, cause, det_cycle, latency, reset_cycle, t_ms, serial;
    char     exp[24];
    uint8_t  mech;
    uint32_t check;
} det_crumb_t;
static det_crumb_t crumb __attribute__((section(".noinit")));

static uint32_t crumb_sum(const det_crumb_t *c)
{
    const uint8_t *b = (const uint8_t *)c;
    uint32_t s = 0x5A5Au;
    for (size_t i = 0; i < offsetof(det_crumb_t, check); i++) {
        s = s * 33u + b[i];
    }
    return s;
}

void det_crumb_save(uint32_t cause, uint32_t reset_cycle)
{
    crumb.magic = CRUMB_MAGIC;
    crumb.cause = cause;
    crumb.det_cycle = det_last.det_cycle;
    crumb.latency = det_last.latency;
    crumb.reset_cycle = reset_cycle;
    crumb.t_ms = HAL_GetTick();
    crumb.serial = det_last.serial;
    crumb.mech = det_last.mech;
    memcpy(crumb.exp, det_last.exp, sizeof crumb.exp);
    crumb.check = crumb_sum(&crumb);
}

/* Reset cause = RCC_CSR flags if the simulator sets any, else the breadcrumb
 * left by the detection (docs/SIMULATOR_LIMITATIONS.md section 3). */
void det_boot(uint32_t csr)
{
    int valid = crumb.magic == CRUMB_MAGIC && crumb.check == crumb_sum(&crumb);
    if (valid) {
        #if RECOVERY
        const char *cause = crumb.cause == DET_CRUMB_WWDG ? "WWDG" : crumb.cause == DET_CRUMB_FAULT ? "FAULT" : crumb.cause == DET_CRUMB_SOFT ? "SOFTWARE" : "?";
#else
        const char *cause = crumb.cause == DET_CRUMB_WWDG ? "WWDG" : crumb.cause == DET_CRUMB_FAULT ? "FAULT" : "?";
#endif
        LOG("RESET", "breadcrumb=%s cause_resolved=%s source=%s csr=0x%08lX exp=%.23s mech=%s det_cycle=%lu "
                     "latency_cycles=%lu det_to_reset_cycles=%lu detect_t_ms=%lu",
            cause, cause, csr != 0u ? "csr+breadcrumb" : "breadcrumb", (unsigned long)csr, crumb.exp,
            det_mech_name((det_mech_t)crumb.mech), (unsigned long)crumb.det_cycle, (unsigned long)crumb.latency,
            (unsigned long)(crumb.reset_cycle - crumb.det_cycle), (unsigned long)crumb.t_ms);
    } else {
        LOG("RESET", "breadcrumb=none cause_resolved=%s source=csr csr=0x%08lX",
            csr != 0u ? board_reset_cause_str(csr) : "COLD_OR_UNKNOWN", (unsigned long)csr);
    }
    crumb.magic = 0; /* consumed */
}

void det_init(void)
{
    extern void det_crc_init(void);
    extern void det_config_init(void);
    det_crc_init();
    det_config_init();
    det_fault_enable();
}

#endif /* PROTECTED */
