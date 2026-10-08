/* Monitor task and the data/stack/heartbeat detectors.
 *
 * The monitor task (priority 1, same as the console) runs every
 * DET_MONITOR_PERIOD_MS. A control task that spins (TIM-01) starves it, which
 * is exactly what the WWDG token (det_wd_alive) detects. */
#include "detect.h"

#if PROTECTED

#include "app.h"
#include "board.h"
#include "dwt.h"
#include "log.h"
#if RECOVERY
#include "recovery.h"
#endif

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* ---- hardware CRC ---------------------------------------------------------- */

void det_crc_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_CRCEN;
}

/* The CRC unit is shared by the sensor, control and monitor tasks; a
 * critical section keeps one computation from being interleaved with
 * another. It is not used from interrupts. */
static uint32_t crc_hw(const uint32_t *w, unsigned n)
{
    int task = __get_IPSR() == 0u && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (task) {
        taskENTER_CRITICAL();
    }
    CRC->CR = CRC_CR_RESET;
    for (unsigned i = 0; i < n; i++) {
        CRC->DR = w[i];
    }
    uint32_t r = CRC->DR;
    if (task) {
        taskEXIT_CRITICAL();
    }
    return r;
}

/* ---- protected data: g_config (CRC + inverted redundant copy) ------------------ */

_Static_assert(sizeof(app_config_t) == 8, "g_config is protected as two CRC words");
static uint32_t cfg_crc;
static int16_t  cfg_inv[4]; /* ~field, so a stuck-at or whole-word wipe cannot match */
static volatile uint32_t cfg_checks, cfg_fail, sample_checks, sample_fail;
static const char *const cfg_field[4] = {"setpoint_centi", "kp_pct_per_c", "out_min", "out_max"};

void det_config_init(void)
{
    uint32_t w[2];
    memcpy(w, &g_config, sizeof w);
    cfg_crc = crc_hw(w, 2);
    const int16_t *f = (const int16_t *)&g_config;
    for (int i = 0; i < 4; i++) {
        cfg_inv[i] = (int16_t)~f[i];
    }
}

int det_check_config(void)
{
    uint32_t w[2];
    int16_t f[4];
    memcpy(w, &g_config, sizeof w);
    memcpy(f, &g_config, sizeof f);
    uint32_t c = crc_hw(w, 2);
    int ok = 1;
    cfg_checks++;
    if (c != cfg_crc) {
        ok = 0;
        det_report(DET_M_CRC, "what=config crc_expected=0x%08lX crc_actual=0x%08lX", (unsigned long)cfg_crc,
                   (unsigned long)c);
    }
    for (int i = 0; i < 4; i++) {
        if (!det_pair_ok(f[i], cfg_inv[i])) {
            ok = 0;
            det_report(DET_M_REDUNDANT, "what=config field=%s value=%d copy=%d", cfg_field[i], f[i],
                       (int)(int16_t)~cfg_inv[i]);
            break;
        }
    }
    if (!ok) {
        cfg_fail++;
    }
    return ok;
}

#if RECOVERY
/* Non-reporting variant used by the checkpoint writer: only a verified-good config is saved. */
int det_config_ok(void)
{
    uint32_t w[2];
    int16_t f[4];
    memcpy(w, &g_config, sizeof w);
    memcpy(f, &g_config, sizeof f);
    if (crc_hw(w, 2) != cfg_crc) {
        return 0;
    }
    for (int i = 0; i < 4; i++) {
        if (!det_pair_ok(f[i], cfg_inv[i])) {
            return 0;
        }
    }
    return 1;
}
#endif

uint32_t det_sample_crc(uint32_t seq, int16_t temp_centi)
{
    uint32_t w[2] = {seq, (uint32_t)(uint16_t)temp_centi};
    return crc_hw(w, 2);
}

/* Verified by the consumer: the CRC the sensor task attached to the sample
 * must match the value the control loop is about to use. */
int det_check_sample(uint32_t seq, int16_t consumed_centi, uint32_t crc, int16_t sensor_centi)
{
    sample_checks++;
    uint32_t c = det_sample_crc(seq, consumed_centi);
    if (c != crc) {
        sample_fail++;
        det_report(DET_M_CRC, "what=sample seq=%lu sensor=%d consumed=%d crc_expected=0x%08lX crc_actual=0x%08lX",
                   (unsigned long)seq, sensor_centi, consumed_centi, (unsigned long)crc, (unsigned long)c);
        return 0;
    }
    return 1;
}

/* ---- I2C ------------------------------------------------------------------------ */

static volatile uint32_t i2c_consec, i2c_fail_total;

void det_i2c_status(int ok, const char *status_name)
{
    if (ok) {
        i2c_consec = 0;
        return;
    }
    i2c_fail_total++;
    if (++i2c_consec == DET_I2C_FAIL_LIMIT) {
        det_report(DET_M_I2C_TIMEOUT, "status=%s consecutive=%lu", status_name, (unsigned long)i2c_consec);
    }
}

/* ---- tasks: stack guards, saved-context seals -------------------------------------- */

typedef struct {
    TaskHandle_t h;
    uint32_t    *base;
    unsigned     words;
    uint32_t     seal[DET_FRAME_WORDS];
    volatile uint8_t sealed;
    uint8_t      peak_pct;
    const char  *name;
} dtask_t;

static dtask_t T[DET_TASK_COUNT];
static const char *const task_name[DET_TASK_COUNT] = {"sensor", "control", "console", "monitor"};
static volatile uint32_t mon_runs, hb_flags;

uint32_t *det_stack_base(det_task_t t) { return T[t].base; }
unsigned  det_stack_words(det_task_t t) { return T[t].words; }

unsigned det_stack_pct_now(det_task_t t)
{
    return T[t].base ? det_stack_pct(det_stack_free_words(T[t].base, T[t].words), T[t].words) : 0u;
}

/* Everything below live_sp is dead stack: paint it again so the usage mark
 * restarts (used by the MEM-04 harness cleanup). */
void det_stack_repaint_dead(det_task_t t, const void *live_sp)
{
    if (T[t].base == NULL) {
        return;
    }
    unsigned upto = (unsigned)(((const uint32_t *)live_sp - T[t].base) - 8); /* keep a margin */
    if (upto > DET_CANARY_WORDS && upto < T[t].words) {
        det_stack_repaint(T[t].base, upto);
    }
}

/* Called inside the scheduler (vTaskSwitchContext) with the outgoing task's
 * context already saved: remember it, so a later change is detectable while
 * the task sleeps. No logging here. */
void det_trace_out(void)
{
    TaskHandle_t cur = xTaskGetCurrentTaskHandle();
    for (int i = 0; i < DET_TASK_MONITOR; i++) {
        if (T[i].h == cur) {
            const uint32_t *top = *(uint32_t *const *)cur; /* TCB[0] = pxTopOfStack */
            memcpy(T[i].seal, top, sizeof T[i].seal);
            T[i].sealed = 1;
            return;
        }
    }
}

static const uint16_t stack_words_cfg[DET_TASK_COUNT] = {STACK_SENSOR, STACK_CONTROL, STACK_CONSOLE, STACK_MONITOR};

static void monitor_task(void *arg);

#if RECOVERY
static volatile uint8_t hb_reset_req;

/* After a task restart: new handle and stack, so rebuild the guards and forget the old heartbeat/seal. Called
 * with the scheduler suspended, before the new task has run. */
void det_task_rebind(det_task_t t)
{
    T[t].h = t == DET_TASK_SENSOR ? g_task_sensor : t == DET_TASK_CONTROL ? g_task_control : g_task_console;
    TaskStatus_t st;
    vTaskGetInfo(T[t].h, &st, pdFALSE, eRunning);
    T[t].base = (uint32_t *)st.pxStackBase;
    T[t].sealed = 0;
    T[t].peak_pct = 0;
    det_stack_prepare(T[t].base, T[t].words, T[t].words - DET_FRAME_WORDS - 2u);
    hb_reset_req = 1;
}
#endif

void det_start(void)
{
    T[DET_TASK_SENSOR].h = g_task_sensor;
    T[DET_TASK_CONTROL].h = g_task_control;
    T[DET_TASK_CONSOLE].h = g_task_console;
    xTaskCreate(monitor_task, "monitor", STACK_MONITOR, NULL, PRIO_MONITOR, &T[DET_TASK_MONITOR].h);
    for (int i = 0; i < DET_TASK_COUNT; i++) {
        T[i].name = task_name[i];
        T[i].words = stack_words_cfg[i];
        TaskStatus_t st;
        vTaskGetInfo(T[i].h, &st, pdFALSE, eRunning);
        T[i].base = (uint32_t *)st.pxStackBase;
        /* canary at the lowest words, painting up to the initial context */
        det_stack_prepare(T[i].base, T[i].words, T[i].words - DET_FRAME_WORDS - 2u);
    }
    LOG("DET", "stack_guards canary_words=%u paint=0x%08lX tasks=sensor,control,console,monitor", (unsigned)DET_CANARY_WORDS,
        (unsigned long)DET_PAINT_WORD);
}

static void monitor_task(void *arg)
{
    LOG("TASK", "name=monitor state=started period_ms=%u", (unsigned)DET_MONITOR_PERIOD_MS);
    TickType_t last = xTaskGetTickCount();
    det_hb_t hb_sensor = {0}, hb_control = {0}, hb_console = {0};
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(DET_MONITOR_PERIOD_MS));
        mon_runs++;
        det_wd_alive(); /* progress token: the monitor got CPU time */
#if RECOVERY
        if (hb_reset_req) {
            hb_reset_req = 0;
            memset(&hb_sensor, 0, sizeof hb_sensor);
            memset(&hb_control, 0, sizeof hb_control);
            memset(&hb_console, 0, sizeof hb_console);
        }
#endif
        if (det_selftest_request) {
            det_selftest_request = 0;
            det_fault_selftest(); /* CPU-03 */
        }
        uint32_t now = HAL_GetTick();
#if RECOVERY
        rec_step(now);
#endif

        /* heartbeats (the per-task progress counters of the application) */
        if (det_hb_step(&hb_sensor, g_state.sensor_hb, now, DET_HB_SENSOR_MS)) {
            hb_flags |= 1u;
            DET_REPORT_T(DET_M_HEARTBEAT, DET_TASK_SENSOR, "task=sensor hb=%lu silent_ms>%u", (unsigned long)g_state.sensor_hb,
                       (unsigned)DET_HB_SENSOR_MS);
        }
        /* the control task is fed by the sensor: judge it only while the sensor is alive */
        if (now - hb_sensor.last_change_ms <= DET_HB_SENSOR_MS) {
            if (det_hb_step(&hb_control, g_state.control_hb, now, DET_HB_CONTROL_MS)) {
                hb_flags |= 2u;
                DET_REPORT_T(DET_M_HEARTBEAT, DET_TASK_CONTROL, "task=control hb=%lu silent_ms>%u", (unsigned long)g_state.control_hb,
                           (unsigned)DET_HB_CONTROL_MS);
            }
        } else {
            hb_control.last_change_ms = now;
        }
        if (det_hb_step(&hb_console, g_state.console_hb, now, DET_HB_CONSOLE_MS)) {
            hb_flags |= 4u;
            DET_REPORT_T(DET_M_HEARTBEAT, DET_TASK_CONSOLE, "task=console hb=%lu silent_ms>%u", (unsigned long)g_state.console_hb,
                       (unsigned)DET_HB_CONSOLE_MS);
        }

        for (int i = 0; i < DET_TASK_MONITOR; i++) {
            /* stack canary */
            if (!det_canary_ok(T[i].base)) {
                DET_REPORT_T(DET_M_STACK_CANARY, i, "task=%s word0=0x%08lX word1=0x%08lX expected=0x%08lX", T[i].name,
                           (unsigned long)T[i].base[0], (unsigned long)T[i].base[1], (unsigned long)DET_CANARY_WORD);
            }
            /* stack painting: high-water mark */
            unsigned pct = det_stack_pct(det_stack_free_words(T[i].base, T[i].words), T[i].words);
            if (pct > T[i].peak_pct) {
                T[i].peak_pct = (uint8_t)pct;
            }
            if (pct > DET_STACK_WARN_PCT) {
                DET_REPORT_T(DET_M_STACK_PAINT, i, "task=%s used_pct=%u warn_pct=%u free_words=%u", T[i].name, pct,
                           (unsigned)DET_STACK_WARN_PCT, det_stack_free_words(T[i].base, T[i].words));
            }
            /* saved context of a sleeping task still equals its seal? */
            if (T[i].sealed) {
                int diff;
                uint32_t was = 0, now_w = 0;
                taskENTER_CRITICAL();
                const uint32_t *top = *(uint32_t *const *)T[i].h;
                diff = det_frame_diff(T[i].seal, top, DET_FRAME_WORDS);
                if (diff >= 0) {
                    was = T[i].seal[diff];
                    now_w = top[diff];
                }
                taskEXIT_CRITICAL();
                if (diff >= 0) {
                    DET_REPORT_T(DET_M_STACK_SEAL, i, "task=%s frame_word=%d was=0x%08lX now=0x%08lX", T[i].name, diff,
                               (unsigned long)was, (unsigned long)now_w);
                }
            }
        }
    }
}

void det_status_print(const char *src)
{
    unsigned ok = 0;
    for (int i = 0; i < DET_TASK_MONITOR; i++) {
        ok += det_canary_ok(T[i].base) ? 1u : 0u;
    }
    LOG("DETSTAT",
        "src=%s mon_runs=%lu det_total=%lu det_false=%lu det_suppressed=%lu wd_refresh=%lu cfg_checks=%lu cfg_fail=%lu "
        "sample_checks=%lu sample_fail=%lu i2c_fail=%lu canary_ok=%u/3 stack_peak_pct=%u:%u:%u hb_flags=%lu",
        src, (unsigned long)mon_runs, (unsigned long)det_count_total, (unsigned long)det_count_false,
        (unsigned long)det_count_suppressed, (unsigned long)det_wd_refreshes, (unsigned long)cfg_checks,
        (unsigned long)cfg_fail, (unsigned long)sample_checks, (unsigned long)sample_fail,
        (unsigned long)i2c_fail_total, ok, T[0].peak_pct, T[1].peak_pct, T[2].peak_pct, (unsigned long)hb_flags);
}

#endif /* PROTECTED */
