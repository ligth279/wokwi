#ifndef DETECT_H
#define DETECT_H

#include "det_logic.h"
#include <stdint.h>

#ifndef PROTECTED
#define PROTECTED 0
#endif
#ifndef RECOVERY
#define RECOVERY 0
#endif

/* Fault-detection layer (protected build only; Step 5). Every mechanism
 * reports through det_report(): the detection is associated with the most
 * recently injected experiment (EXP id), stamped with the DWT cycle at which
 * the check found the fault, and its latency from the injection is computed.
 * Detection stops at the report: nothing here recovers from a fault.
 *
 *   mechanism     detects                                   checked by
 *   WWDG          hang: monitor task no longer runs         TIM2 ISR (1 kHz)
 *   FAULT_HANDLER MemManage/Bus/Usage/HardFault exception   exception handlers
 *   STACK_CANARY  guard words at the stack base overwritten monitor task (50 Hz)
 *   STACK_SEAL    saved task context changed while blocked  monitor task
 *   STACK_PAINT   stack use above the warning level         monitor task
 *   CRC           g_config / sensor sample CRC mismatch     control task, monitor
 *   REDUNDANT     g_config field differs from its copy      control task, monitor
 *   HEARTBEAT     task heartbeat missing                    monitor task
 *   I2C_TIMEOUT   consecutive failed sensor transactions    sensor task
 */
typedef enum {
    DET_M_NONE = 0,
    DET_M_WWDG,
    DET_M_FAULT_HANDLER,
    DET_M_STACK_CANARY,
    DET_M_STACK_SEAL,
    DET_M_STACK_PAINT,
    DET_M_CRC,
    DET_M_REDUNDANT,
    DET_M_HEARTBEAT,
    DET_M_I2C_TIMEOUT,
    DET_M_COUNT
} det_mech_t;

typedef enum { DET_TASK_SENSOR = 0, DET_TASK_CONTROL, DET_TASK_CONSOLE, DET_TASK_MONITOR, DET_TASK_COUNT } det_task_t;

const char *det_mech_name(det_mech_t m);

#if PROTECTED

#define DET_MONITOR_PERIOD_MS   20u
#define DET_STACK_WARN_PCT      75u   /* STACK_PAINT threshold (measured normal peaks: sensor 45 %, control 52 %, console 67 %) */
#define DET_HB_SENSOR_MS        300u  /* three sensor periods */
#define DET_HB_CONTROL_MS       300u
#define DET_HB_CONSOLE_MS       500u
#define DET_WD_STALE_MS         150u  /* monitor silence before the WWDG is no longer refreshed */
#define DET_WD_SIM_TIMEOUT_MS   8u    /* Wokwi: ~7.28 ms WWDG timeout after the last refresh (prescaler ignored) */
#define DET_I2C_FAIL_LIMIT      3u

/* Framework (detect.c) */
void det_report(det_mech_t m, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#if RECOVERY
/* Same, naming the affected task so the recovery layer restarts the right one. */
void det_report_task(det_mech_t m, int task, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#define DET_REPORT_T(m, t, ...) det_report_task((m), (t), __VA_ARGS__)
#else
#define DET_REPORT_T(m, t, ...) det_report((m), __VA_ARGS__)
#endif
void det_init(void);       /* before the tasks exist: CRC unit, fault handlers, config copies */
void det_boot(uint32_t csr); /* after the RESET line: resolve and log the reset cause */
void det_start(void);      /* after the application tasks exist: stack guards, monitor task */
void det_status_print(const char *src);

typedef struct {
    char     exp[24];
    uint32_t det_cycle, inj_cycle, latency, serial;
    uint8_t  mech;
} det_last_t;
extern det_last_t det_last;
extern volatile uint32_t det_count_total, det_count_false, det_count_suppressed;

/* Persisted reset breadcrumb (.noinit), for resets caused by a detection. */
#define DET_CRUMB_WWDG   1u
#define DET_CRUMB_FAULT  2u
#define DET_CRUMB_SOFT   3u   /* controlled software reset (recovery level 3) */
void det_crumb_save(uint32_t cause, uint32_t reset_cycle);

/* Application hooks (monitor, det_monitor.c) */
int  det_check_config(void);                   /* control task, before using g_config */
uint32_t det_sample_crc(uint32_t seq, int16_t temp_centi);
int  det_check_sample(uint32_t seq, int16_t consumed_centi, uint32_t crc, int16_t sensor_centi);
void det_i2c_status(int ok, const char *status_name);
void det_trace_out(void);                      /* traceTASK_SWITCHED_OUT: seals the saved context */
uint32_t *det_stack_base(det_task_t t);        /* NULL until det_start() */
unsigned det_stack_words(det_task_t t);
unsigned det_stack_pct_now(det_task_t t);      /* current use, percent, from the painting */
void det_stack_repaint_dead(det_task_t t, const void *live_sp); /* repaint the dead stack below live_sp */

/* WWDG (det_wwdg.c) */
void det_wwdg_start(void);                     /* right before the scheduler starts */
void det_wd_alive(void);                       /* monitor task: progress token */
extern volatile uint32_t det_wd_refreshes;

/* Fault handlers (det_fault.c) */
void det_fault_enable(void);
void det_fault_selftest(void);                 /* synthetic invocation of the capture path (not a CPU fault) */
extern volatile uint32_t det_fault_selftest_done;
extern volatile uint8_t  det_selftest_request; /* CPU-03: run by the monitor task, so the report is made after INJECTED */

/* Detector validation faults (fault_det.c): MEM-03 canary overwrite, MEM-04 stack
 * over-use, CPU-03 synthetic fault-handler invocation. They exist only in the
 * protected build; they are not part of the nine study faults. */
void     fd_mem03_inject(uint32_t *b, uint32_t *a);
int      fd_mem03_observe(void);
void     fd_mem03_cleanup(void);
uint32_t fd_mem03_read(void);
void     fd_mem04_inject(uint32_t *b, uint32_t *a);
int      fd_mem04_observe(void);
uint32_t fd_mem04_read(void);
void     fd_mem04_cleanup(void);
#if RECOVERY
void     fd_tim03_inject(uint32_t *b, uint32_t *a);
int      fd_tim03_observe(void);
uint32_t fd_tim03_read(void);
int      fd_tim03_active(void);
#endif
void     fd_cpu03_inject(uint32_t *b, uint32_t *a);
int      fd_cpu03_observe(void);
uint32_t fd_cpu03_read(void);

#endif /* PROTECTED */
#endif /* DETECT_H */
