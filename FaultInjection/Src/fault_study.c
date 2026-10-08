/* Injection routines of the nine study faults. See fault_study.h for the
 * summary table and fault_catalog.h for the routine contract.
 *
 * Nothing here runs unless the framework calls it for an accepted FAULT
 * command; all state is armed by the injection and consumed once. */
#include "fault_study.h"

#if FI_STUDY_FAULTS

#include "app.h"
#include "board.h"
#include "fault_fw.h"
#include "log.h"

#include "FreeRTOS.h"
#include "task.h"
#if RECOVERY
#include "detect.h"
#include "fault_inject.h"
#endif

static const app_config_t nominal_cfg = APP_CONFIG_DEFAULT;

#if RECOVERY
/* With recovery the effect can be undone before the observation runs, so "detected for this injection" also counts. */
#define FS_DETECTED() (det_last.serial != 0u && det_last.serial == g_faults_injected)
#else
#define FS_DETECTED() 0
#endif

/* ---- MEM-01: SRAM variable bit flip ---------------------------------------
 * Target: g_config.setpoint_centi (an application variable in .data).
 * Flipping bit 10 turns 2200 into 3224 centi-C, so the controller sees the
 * temperature far below its setpoint and drives the output to its minimum.
 * Observed when the last control output differs from what the nominal
 * configuration would have produced for the same input. */

void fs_mem01_inject(uint32_t *b, uint32_t *a)
{
    uint16_t v = (uint16_t)g_config.setpoint_centi;
    *b = v;
    v ^= (uint16_t)(1u << FS_MEM01_BIT);
    g_config.setpoint_centi = (int16_t)v;
    *a = v;
}

int fs_mem01_observe(void)
{
    return control_compute(g_state.last_input, &nominal_cfg) != g_state.output || FS_DETECTED();
}

/* Experiment-harness cleanup (not a recovery mechanism): restore the
 * variable so the next experiment in the same run starts clean. */
void fs_mem01_cleanup(void)
{
    g_config.setpoint_centi = nominal_cfg.setpoint_centi;
}

uint32_t fs_mem01_read(void)
{
    return (uint16_t)g_config.setpoint_centi;
}

/* ---- MEM-02: stack corruption ----------------------------------------------
 * Target: the return address (LR slot) in the saved exception frame on the
 * sensor task's stack. The task is blocked in vTaskDelayUntil, so its
 * context was saved by port_switch_context (RTOS/port_wokwi_cm3/port.c):
 *   pxTopOfStack[0..7]  R4..R11 (software saved)
 *   pxTopOfStack[8..15] R0 R1 R2 R3 R12 LR PC xPSR (hardware frame)
 * LR (index 13) is the address vPortYield() returns to. Bit 29 is flipped,
 * so when the task wakes (<= SENSOR_PERIOD_MS later) it returns to an
 * unmapped address 0x28xxxxxx. The first word of a FreeRTOS TCB is
 * pxTopOfStack, which is how the slot is located. */
#define SAVED_LR_INDEX 13u
#ifndef MEM02_MASK
#define MEM02_MASK     (1u << 29)
#endif

static volatile uint32_t *mem02_slot(void)
{
    uint32_t *top = *(uint32_t *volatile *)g_task_sensor;
    return &top[SAVED_LR_INDEX];
}

void fs_mem02_plan(uint32_t *b, uint32_t *a)
{
    *b = *mem02_slot();
    *a = *b ^ MEM02_MASK;
}

void fs_mem02_inject(uint32_t *b, uint32_t *a)
{
    volatile uint32_t *slot = mem02_slot();
    *b = *slot;
    *slot = *b ^ MEM02_MASK;
    *a = *slot;
}

#if RECOVERY
int fs_mem02_observe(void)
{
    return FS_DETECTED();
}
#endif

/* ---- CPU-01: program counter corruption ----------------------------------
 * The plan samples the PC (address inside the injector) and computes the
 * corrupted value: bit 29 flipped, Thumb bit set. inject() then loads it
 * into the PC with BX, so the CPU really continues at the corrupted
 * address (0x28xxxxxx, unmapped) instead of returning to the caller. */
#ifndef FS_CPU01_MASK
#define FS_CPU01_MASK (1u << 29)
#endif
static uint32_t cpu01_target;

void fs_cpu01_plan(uint32_t *b, uint32_t *a)
{
    uint32_t pc;
    __asm volatile("mov %0, pc" : "=r"(pc));
    cpu01_target = (pc ^ FS_CPU01_MASK) | 1u;
    *b = pc;
    *a = cpu01_target;
}

void fs_cpu01_inject(uint32_t *b, uint32_t *a)
{
    __asm volatile("bx %0" ::"r"(cpu01_target));
    __builtin_unreachable();
}

/* ---- CPU-02: stack pointer corruption ------------------------------------
 * The plan samples SP (PSP, since this runs in a task) and computes the
 * corrupted value (bit 28 flipped: 0x2000xxxx -> 0x3000xxxx, outside the
 * 20 KB SRAM). inject() loads it into SP and returns normally: the callers'
 * epilogues then pop their saved registers and return address from the
 * corrupted pointer, which is what a corrupted SP does to running code. */
#ifndef FS_CPU02_MASK
#define FS_CPU02_MASK (1u << 28)
#endif
static uint32_t cpu02_target;

void fs_cpu02_plan(uint32_t *b, uint32_t *a)
{
    uint32_t sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    cpu02_target = sp ^ FS_CPU02_MASK;
    *b = sp;
    *a = cpu02_target;
}

void fs_cpu02_inject(uint32_t *b, uint32_t *a)
{
    __asm volatile("mov sp, %0" ::"r"(cpu02_target) : "memory");
}

/* ---- TIM-01: infinite loop -----------------------------------------------
 * Runs in the injecting context. Through the UART mechanism that is the
 * control task: it stops consuming samples and, being higher priority than
 * the console, starves it, so the output stops entirely. */
void fs_tim01_plan(uint32_t *b, uint32_t *a)
{
    *b = g_state.control_hb;
    *a = g_state.control_hb;
}

void fs_tim01_inject(uint32_t *b, uint32_t *a)
{
    for (;;) {
        __asm volatile("" ::: "memory");
    }
}

/* ---- TIM-02: blocked task ------------------------------------------------
 * inject() requests the stall; the sensor task honours it at the top of its
 * next period (fi_study_site_sensor) by blocking on a notification nobody
 * will send. Its heartbeat then stops; the control task, fed only by the
 * sensor, stops too; the console keeps running. */
static volatile uint8_t  tim02_request, tim02_blocked;
static volatile uint32_t tim02_hb_at_block, tim02_block_ms;

void fs_tim02_inject(uint32_t *b, uint32_t *a)
{
    *b = g_state.sensor_hb;
    tim02_request = 1;
    *a = g_state.sensor_hb;
}

void fi_study_site_sensor(void)
{
#if RECOVERY
    if (tim02_request || fd_tim03_active()) {
#else
    if (tim02_request) {
#endif
        tim02_request = 0;
        tim02_hb_at_block = g_state.sensor_hb;
        tim02_block_ms = HAL_GetTick();
        tim02_blocked = 1;
        LOG("TASK", "name=sensor state=blocked reason=fault_injection exp=%s hb=%lu", fi_cur_exp_id,
            (unsigned long)tim02_hb_at_block);
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

int fs_tim02_observe(void)
{
    return (tim02_blocked && g_state.sensor_hb == tim02_hb_at_block &&
            (HAL_GetTick() - tim02_block_ms) >= FS_TIM02_STALL_MS) ||
           FS_DETECTED();
}

uint32_t fs_tim02_read(void)
{
    return g_state.sensor_hb;
}

/* ---- DATA-01: sensor data corruption -------------------------------------
 * The sample delivered to the control task is replaced once by 8500
 * (85.00 C, far above the 22-28 C the sensor produces). The sensor task and
 * the real sensor value are untouched; only the control input of one cycle
 * is wrong. */
static volatile uint8_t data01_armed;

void fs_data01_inject(uint32_t *b, uint32_t *a)
{
    *b = (uint16_t)g_state.sensor_in; /* the value this cycle would have used */
    data01_armed = 1;
    *a = (uint16_t)FS_DATA01_VALUE;
}

int16_t fi_study_sensor_hook(int16_t centi)
{
    if (data01_armed) {
        data01_armed = 0;
        return FS_DATA01_VALUE;
    }
    return centi;
}

int fs_data01_observe(void)
{
    return (!data01_armed && g_state.last_input == FS_DATA01_VALUE) || FS_DETECTED();
}

void fs_data01_cleanup(void)
{
    data01_armed = 0;
}

uint32_t fs_data01_read(void)
{
    return (uint16_t)g_state.last_input;
}

/* ---- DATA-02: configuration data corruption ------------------------------
 * Target: g_config.kp_pct_per_c (proportional gain, 15 -> 100). Reaches the
 * control law output = clamp((temp - setpoint) * kp / 100). */
void fs_data02_inject(uint32_t *b, uint32_t *a)
{
    *b = (uint16_t)g_config.kp_pct_per_c;
    g_config.kp_pct_per_c = FS_DATA02_KP;
    *a = (uint16_t)g_config.kp_pct_per_c;
}

int fs_data02_observe(void)
{
    return control_compute(g_state.last_input, &nominal_cfg) != g_state.output || FS_DETECTED();
}

void fs_data02_cleanup(void)
{
    g_config.kp_pct_per_c = nominal_cfg.kp_pct_per_c;
}

uint32_t fs_data02_read(void)
{
    return (uint16_t)g_config.kp_pct_per_c;
}

/* ---- PERIPH-01: I2C SDA held low ----------------------------------------
 * Raises the TRIG input of the custom i2c-stuck Wokwi chip (PB0), which
 * then drives SDA low (chips/i2c-stuck.chip.c). The fault is permanent in
 * the baseline: nothing releases TRIG. Observed when a sensor transaction
 * has failed since the injection. */
static uint32_t periph01_errors_at;

void fs_periph01_inject(uint32_t *b, uint32_t *a)
{
    *b = (uint32_t)HAL_GPIO_ReadPin(FAULT_I2C_TRIG_PORT, FAULT_I2C_TRIG_PIN);
    periph01_errors_at = g_state.sensor_errors;
    HAL_GPIO_WritePin(FAULT_I2C_TRIG_PORT, FAULT_I2C_TRIG_PIN, GPIO_PIN_SET);
    *a = (uint32_t)HAL_GPIO_ReadPin(FAULT_I2C_TRIG_PORT, FAULT_I2C_TRIG_PIN);
}

int fs_periph01_observe(void)
{
    return g_state.sensor_errors > periph01_errors_at || FS_DETECTED();
}

uint32_t fs_periph01_read(void)
{
    return (uint32_t)HAL_GPIO_ReadPin(FAULT_I2C_TRIG_PORT, FAULT_I2C_TRIG_PIN);
}

#endif /* FI_STUDY_FAULTS */
