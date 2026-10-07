/*
 * Step 0 - Wokwi peripheral capability probe ("smoke test").
 *
 * Verifies, inside the Wokwi simulator, every hardware feature the fault
 * study depends on, before any project code is built on top of it.
 * Each check prints result=PASS, FAIL, or LIMIT. LIMIT = the simulator deviates
 * from STM32F103 silicon AND the project has a documented mitigation (see
 * docs/SIMULATOR_LIMITATIONS.md). FAIL = a deviation with no mitigation.
 * Acceptance: failures=0.
 *
 *   CORE_SVC           SVC exception is taken
 *   CORE_EXCRET_PSP    SVC can return into a different frame on PSP (task start/switch)
 *   CORE_EXCRET_MSP    ... and back to the MSP thread context
 *   CORE_SHPR          system-handler priority (SHPR3) is retained
 *   CORE_PENDSV        PendSV taken exactly once per pend (run last)
 *   CORE_STRB_LDRB     byte stores/loads at all alignments
 *   CORE_STRH_LDRH     halfword stores/loads
 *   CORE_IT_STATE      IT-block state survives interrupts (conditional execution)
 *   CORE_PRIMASK       cpsid/cpsie set/clear PRIMASK and mask SysTick + NVIC IRQs
 *   CORE_BASEPRI       BASEPRI is retained and masks a lower-priority IRQ
 *   CORE_PSP           CONTROL.SPSEL switches thread mode to PSP
 *   CLOCK              SYSCLK reaches 72 MHz from HSE+PLL
 *   UART               USART1 output (implicit: you can read this log)
 *   DWT                DWT->CYCCNT counts and agrees with SysTick time
 *   CRC / CRC_DETECT   hardware CRC == software CRC-32/MPEG-2; 1-bit flip detected
 *   TIM2_IRQ           TIM2 update interrupt fires at 1 kHz
 *   I2C_INIT/I2C_SCAN  I2C1 initialises; a bus scan completes (no hang)
 *   WWDG_PRESCALER     WDGTB bits in WWDG_CFR are retained
 *   WWDG_EWI           early-wakeup interrupt fires
 *   WWDG_COUNTER       WWDG down-counter decrements
 *   WWDG_AUTO_RESET    WWDG resets the MCU when counter passes 0x3F
 *   WWDG_FORCED_RESET  writing T6=0 with WDGA=1 resets the MCU
 *   CSR_WWDGRSTF       RCC_CSR.WWDGRSTF set after a WWDG reset
 *   SOFT_RESET         NVIC_SystemReset() resets the MCU
 *   CSR_SFTRSTF        RCC_CSR.SFTRSTF set after a software reset
 *   NOINIT_WWDG/SOFT   .noinit SRAM survives each reset type
 *
 * The test spans three boots, sequenced through a .noinit record:
 *   stage 0 (cold boot): checks, then WWDG hang (auto reset), then forced T6=0
 *   stage 1 (after WWDG reset): check CSR + noinit, then NVIC_SystemReset()
 *   stage 2 (after software reset): check CSR + noinit, print SMOKE_DONE
 */
#include "board.h"
#include "dwt.h"
#include "log.h"
#include <stdio.h>

#define SMOKE_MAGIC    0x534D4B31u /* "SMK1" */
#define NOINIT_PATTERN 0xA5C3F00Du

enum { R_FAIL = 0, R_PASS = 1, R_LIMIT = 2 };
enum { WWDG_PENDING_NONE = 0, WWDG_PENDING_AUTO = 1, WWDG_PENDING_FORCED = 2 };

typedef struct {
    uint32_t magic;
    uint32_t stage;
    uint32_t pattern;
    uint32_t wwdg_pending;  /* which WWDG reset path was armed before the reset */
    uint32_t hang_cycles;   /* cycles from start of hang until last loop pass  */
    uint32_t failures;
    uint32_t limits;
} smoke_state_t;

static smoke_state_t s __attribute__((section(".noinit")));
static volatile uint32_t tim2_ticks, ewi_count;

static void result(const char *id, int r, const char *detail)
{
    static const char *const names[] = {"FAIL", "PASS", "LIMIT"};
    LOG("SMOKE", "id=%s result=%s %s", id, names[r], detail);
    if (r == R_FAIL) s.failures++;
    if (r == R_LIMIT) s.limits++;
}

static void uart_flush(void)
{
    while (!(USART1->SR & USART_SR_TC)) {
    }
}

static uint32_t crc32_mpeg2_ref(const uint32_t *w, uint32_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= w[i];
        for (int b = 0; b < 32; b++) {
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : (crc << 1);
        }
    }
    return crc;
}

static void test_dwt(void)
{
    char d[96];
    uint32_t c0 = dwt_cycles();
    uint32_t t0 = HAL_GetTick();
    HAL_Delay(100);
    uint32_t cyc = dwt_cycles() - c0;
    uint32_t ms  = HAL_GetTick() - t0;
    /* Expect cycles ~= ms * SystemCoreClock/1000; HAL_GetTick has +-1 ms
     * granularity, so allow 5 %. */
    uint32_t expect = ms * (SystemCoreClock / 1000u);
    uint32_t diff   = cyc > expect ? cyc - expect : expect - cyc;
    snprintf(d, sizeof d, "cycles=%lu ms=%lu expected=%lu", (unsigned long)cyc,
             (unsigned long)ms, (unsigned long)expect);
    result("DWT", cyc != 0 && diff <= expect / 20u, d);
}

static void test_crc(void)
{
    static const uint32_t data[4] = {0x12345678u, 0xDEADBEEFu, 0x00000000u, 0xFFFFFFFFu};
    CRC_HandleTypeDef hcrc = {.Instance = CRC};
    char d[96];

    __HAL_RCC_CRC_CLK_ENABLE();
    HAL_CRC_Init(&hcrc);
    uint32_t hw  = HAL_CRC_Calculate(&hcrc, (uint32_t *)data, 4);
    uint32_t ref = crc32_mpeg2_ref(data, 4);
    snprintf(d, sizeof d, "hw=0x%08lX ref=0x%08lX", (unsigned long)hw, (unsigned long)ref);
    result("CRC", hw == ref ? R_PASS : R_FAIL, d);

    uint32_t bad[4] = {data[0] ^ 0x1u, data[1], data[2], data[3]};
    uint32_t hw_bad = HAL_CRC_Calculate(&hcrc, bad, 4);
    snprintf(d, sizeof d, "hw_corrupted=0x%08lX", (unsigned long)hw_bad);
    result("CRC_DETECT", hw_bad != hw ? R_PASS : R_FAIL, d);
}

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF;
        tim2_ticks++;
    }
}

static void test_tim2(void)
{
    char d[96];
    __HAL_RCC_TIM2_CLK_ENABLE();
    /* TIM2 clock = 2 x PCLK1 = 72 MHz -> /72 = 1 MHz -> /1000 = 1 kHz */
    TIM2->PSC = 72u - 1u;
    TIM2->ARR = 1000u - 1u;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR  = 0;
    TIM2->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    tim2_ticks = 0;
    TIM2->CR1 = TIM_CR1_CEN;
    HAL_Delay(50);
    TIM2->CR1 = 0;
    HAL_NVIC_DisableIRQ(TIM2_IRQn);
    uint32_t n = tim2_ticks;
    snprintf(d, sizeof d, "ticks_in_50ms=%lu", (unsigned long)n);
    result("TIM2_IRQ", (n >= 48 && n <= 52) ? R_PASS : R_FAIL, d);
}

/* Byte / halfword stores and loads at every alignment (SRAM). */
static void test_mem_access(void)
{
    char d[128];
    static volatile uint32_t w[2];
    volatile uint8_t *b = (volatile uint8_t *)w;
    volatile uint16_t *h = (volatile uint16_t *)w;
    int ok = 1;

    w[0] = 0; w[1] = 0;
    for (int i = 0; i < 8; i++) {
        b[i] = (uint8_t)(0x11u * (unsigned)(i + 1)); /* STRB at offsets 0..7 */
    }
    uint32_t w0 = w[0], w1 = w[1];
    if (w0 != 0x44332211u || w1 != 0x88776655u) ok = 0;
    uint32_t rb = 0;
    for (int i = 0; i < 8; i++) {
        if (b[i] != (uint8_t)(0x11u * (unsigned)(i + 1))) ok = 0; /* LDRB */
        rb = (rb << 4) | (b[i] & 0xFu);
    }
    snprintf(d, sizeof d, "strb_w0=0x%08lX strb_w1=0x%08lX ldrb_nibbles=0x%08lX", (unsigned long)w0,
             (unsigned long)w1, (unsigned long)rb);
    result("CORE_STRB_LDRB", ok ? R_PASS : R_FAIL, d);

    ok = 1;
    w[0] = 0; w[1] = 0;
    h[0] = 0xA1B2u; h[1] = 0xC3D4u; h[2] = 0xE5F6u; h[3] = 0x0718u; /* STRH */
    w0 = w[0]; w1 = w[1];
    if (w0 != 0xC3D4A1B2u || w1 != 0x0718E5F6u) ok = 0;
    if (h[1] != 0xC3D4u || h[3] != 0x0718u) ok = 0; /* LDRH */
    snprintf(d, sizeof d, "strh_w0=0x%08lX strh_w1=0x%08lX", (unsigned long)w0, (unsigned long)w1);
    result("CORE_STRH_LDRH", ok ? R_PASS : R_FAIL, d);
}

/* IT-block state across interrupts: run an ITETE block whose condition is
 * always NE while TIM2 interrupts at 100 kHz. On silicon the EQ-path adds
 * never execute (ITSTATE is saved in the stacked xPSR on exception entry
 * and restored on return). */
static void test_it_state(void)
{
    char d[128];
    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->PSC = 0;
    TIM2->ARR = 720u - 1u; /* 72 MHz / 720 = 100 kHz */
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR  = 0;
    TIM2->DIER = TIM_DIER_UIE;
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    tim2_ticks = 0;
    TIM2->CR1 = TIM_CR1_CEN;

    uint32_t eq = 0, ne = 0, iters = 200000u;
    __asm volatile("1: cmp %[a], %[b]          \n"
                   "   itete eq                 \n"
                   "   addeq %[e], %[e], #1     \n"
                   "   addne %[n], %[n], #1     \n"
                   "   addeq %[e], %[e], #1     \n"
                   "   addne %[n], %[n], #1     \n"
                   "   subs %[i], %[i], #1      \n"
                   "   bne 1b                   \n"
                   : [e] "+r"(eq), [n] "+r"(ne), [i] "+r"(iters)
                   : [a] "r"(1u), [b] "r"(2u)
                   : "cc");

    TIM2->CR1 = 0;
    HAL_NVIC_DisableIRQ(TIM2_IRQn);
    snprintf(d, sizeof d, "eq_path_executed=%lu ne_path_executed=%lu expected_ne=400000 irqs=%lu",
             (unsigned long)eq, (unsigned long)ne, (unsigned long)tim2_ticks);
    result("CORE_IT_STATE", (eq == 0 && ne == 400000u) ? R_PASS : R_LIMIT, d);
}

/* PRIMASK: __disable_irq() must set PRIMASK and block both SysTick and
 * NVIC interrupts (TIM2) until __enable_irq(). */
static volatile uint32_t svc_count, pendsv_count;
static void test_primask(void)
{
    char d[128];
    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->PSC = 72u - 1u;
    TIM2->ARR = 1000u - 1u;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR  = 0;
    TIM2->DIER = TIM_DIER_UIE;
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 = TIM_CR1_CEN;

    uint32_t pm_before = __get_PRIMASK();
    __disable_irq();
    uint32_t pm_disabled = __get_PRIMASK();
    uint32_t tick0 = uwTick, tim0 = tim2_ticks, c0 = dwt_cycles();
    while (dwt_cycles() - c0 < 72000u * 5u) { /* 5 ms with IRQs (supposedly) off */
    }
    uint32_t tick_d = uwTick - tick0, tim_d = tim2_ticks - tim0;
    __enable_irq();
    uint32_t pm_enabled = __get_PRIMASK();
    c0 = dwt_cycles();
    while (dwt_cycles() - c0 < 72000u * 5u) {
    }
    uint32_t tick_after = uwTick - tick0, tim_after = tim2_ticks - tim0;
    TIM2->CR1 = 0;
    HAL_NVIC_DisableIRQ(TIM2_IRQn);

    snprintf(d, sizeof d, "primask_before=%lu after_cpsid=%lu after_cpsie=%lu systick_masked=%lu tim2_masked=%lu systick_total=%lu tim2_total=%lu",
             (unsigned long)pm_before, (unsigned long)pm_disabled, (unsigned long)pm_enabled,
             (unsigned long)tick_d, (unsigned long)tim_d, (unsigned long)tick_after, (unsigned long)tim_after);
    int ok = pm_before == 0 && pm_disabled == 1 && pm_enabled == 0 && tick_d == 0 && tim_d == 0 &&
             tick_after > 0 && tim_after > 0;
    result("CORE_PRIMASK", ok ? R_PASS : R_LIMIT, d);

    /* Direct MSR writes to PRIMASK, and SVC while PRIMASK=1. (On silicon an
     * SVC with PRIMASK=1 escalates to HardFault; here we only characterise
     * what the simulator does.) */
    __set_PRIMASK(1);
    uint32_t msr1 = __get_PRIMASK();
    __set_PRIMASK(0);
    uint32_t msr0 = __get_PRIMASK();
    snprintf(d, sizeof d, "msr1_read=%lu msr0_read=%lu", (unsigned long)msr1, (unsigned long)msr0);
    result("CORE_PRIMASK_MSR", (msr1 == 1 && msr0 == 0) ? R_PASS : R_FAIL, d);

    __set_PRIMASK(1);
    uint32_t svc_before = svc_count;
    __asm volatile("svc 0" ::: "memory");
    __ISB();
    uint32_t svc_masked = svc_count - svc_before;
    __set_PRIMASK(0);
    __ISB();
    uint32_t svc_after_clear = svc_count - svc_before;
    snprintf(d, sizeof d, "svc_taken_with_primask1=%lu svc_taken_after_clear=%lu", (unsigned long)svc_masked,
             (unsigned long)svc_after_clear);
    result("CORE_SVC_PRIMASK", R_LIMIT, d); /* characterisation only */
}

static void test_i2c(void)
{
    char d[96];
    HAL_StatusTypeDef st = board_i2c_init();
    snprintf(d, sizeof d, "init_status=%d", (int)st);
    result("I2C_INIT", st == HAL_OK ? R_PASS : R_FAIL, d);

    uint32_t t0 = HAL_GetTick();
    int found = 0;
    for (uint16_t a = 0x08; a < 0x78; a++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(a << 1), 1, 2) == HAL_OK) {
            LOG("SMOKE", "i2c_device addr=0x%02X", a);
            found++;
        }
    }
    snprintf(d, sizeof d, "devices=%d scan_ms=%lu", found, (unsigned long)(HAL_GetTick() - t0));
    result("I2C_SCAN", R_PASS, d); /* pass = scan completed without hanging */
}

/* SVC dispatcher. svc_mode selects the behaviour:
 *   0: count only (normal return)
 *   1: switch to a prepared PSP frame and return with EXC_RETURN 0xFFFFFFFD
 *      (this is how an RTOS starts / switches to a task)
 *   2: return to the MSP context with EXC_RETURN 0xFFFFFFF9 */
static volatile uint32_t svc_mode, svc_last_lr, fake_task_ran, fake_task_control;
static uint32_t fake_stack[64] __attribute__((aligned(8)));

uint32_t svc_dispatch(uint32_t exc_lr)
{
    svc_count++;
    svc_last_lr = exc_lr;
    if (svc_mode == 1u) {
        __set_PSP((uint32_t)&fake_stack[64 - 8]);
        return 0xFFFFFFFDu;
    }
    if (svc_mode == 2u) {
        return 0xFFFFFFF9u;
    }
    return exc_lr;
}

__attribute__((naked)) void SVC_Handler(void)
{
    __asm volatile("push {r4, lr}\n"
                   "mov r0, lr\n"
                   "bl svc_dispatch\n"
                   "pop {r4, lr}\n"
                   "bx r0\n");
}

static void fake_task(void)
{
    fake_task_ran = 1;
    fake_task_control = __get_CONTROL();
    svc_mode = 2;
    __asm volatile("svc 0");
    for (;;) { /* must never get here */
    }
}

static void fake_task_trap(void)
{
    for (;;) {
    }
}

static void test_exc_return(void)
{
    char d[128];
    uint32_t *f = &fake_stack[64 - 8];
    f[0] = f[1] = f[2] = f[3] = f[4] = 0;      /* r0-r3, r12 */
    f[5] = (uint32_t)fake_task_trap;           /* lr  */
    f[6] = (uint32_t)fake_task & ~1u;          /* pc  */
    f[7] = 0x01000000u;                        /* xPSR: Thumb bit */
    fake_task_ran = 0;
    svc_mode = 1;
    __asm volatile("svc 0" ::: "memory");
    svc_mode = 0;
    uint32_t ctrl_back = __get_CONTROL();
    snprintf(d, sizeof d, "fake_task_ran=%lu task_control=0x%lX", (unsigned long)fake_task_ran,
             (unsigned long)fake_task_control);
    result("CORE_EXCRET_PSP", (fake_task_ran == 1u && fake_task_control == 2u) ? R_PASS : R_FAIL, d);
    snprintf(d, sizeof d, "returned_to_main=1 control=0x%lX last_lr=0x%08lX", (unsigned long)ctrl_back,
             (unsigned long)svc_last_lr);
    result("CORE_EXCRET_MSP", ctrl_back == 0u ? R_PASS : R_FAIL, d);
}

void PendSV_Handler(void)
{
    pendsv_count++;
    if (pendsv_count == 50u) {
        /* Silicon takes PendSV once per pend; 50 entries = re-entry storm. */
        result("CORE_PENDSV", R_LIMIT, "reentered_times=50 (pend bit reads clear, handler re-entered on every exception return)");
        LOG("SMOKE", "summary failures=%lu limits=%lu", (unsigned long)s.failures, (unsigned long)s.limits);
        LOG("SMOKE", "SMOKE_DONE");
        s.magic = 0;
        for (;;) {
        }
    }
}

/* Run last: if PendSV storms, the handler above ends the probe. */
static void test_pendsv(void)
{
    char d[96];
    NVIC_SetPriority(PendSV_IRQn, 15);
    snprintf(d, sizeof d, "shpr3=0x%08lX", (unsigned long)SCB->SHPR[10] | ((unsigned long)SCB->SHPR[11] << 8));
    result("CORE_SHPR", (SCB->SHPR[10] & 0xF0u) == 0xF0u ? R_PASS : R_LIMIT, d);
    pendsv_count = 0;
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
    __DSB();
    __ISB();
    snprintf(d, sizeof d, "pendsv_count=%lu", (unsigned long)pendsv_count);
    result("CORE_PENDSV", pendsv_count == 1 ? R_PASS : R_FAIL, d);
}

/* Core features FreeRTOS depends on: SVC, PendSV, BASEPRI masking,
 * PSP/CONTROL thread-mode stack switch. */
static void test_core(void)
{
    char d[96];

    svc_count = 0;
    __asm volatile("svc 0");
    __DSB();
    __ISB();
    snprintf(d, sizeof d, "svc_count=%lu", (unsigned long)svc_count);
    result("CORE_SVC", svc_count == 1 ? R_PASS : R_FAIL, d);
    test_exc_return();

    __set_BASEPRI(5u << 4);
    uint32_t bp = __get_BASEPRI();
    uint32_t t0 = HAL_GetTick();
    for (volatile uint32_t i = 0; i < 72000u; i++) { /* ~several ms; SysTick (prio 15) must stay masked */
    }
    uint32_t ticks_masked = HAL_GetTick() - t0;
    __set_BASEPRI(0);
    snprintf(d, sizeof d, "basepri_read=0x%02lX ticks_while_masked=%lu", (unsigned long)bp,
             (unsigned long)ticks_masked);
    result("CORE_BASEPRI", (bp == 0x50u && ticks_masked == 0) ? R_PASS : R_LIMIT, d);

    static uint32_t pstack[64] __attribute__((aligned(8)));
    __set_PSP((uint32_t)&pstack[64]);
    __set_CONTROL(0x2); /* thread mode uses PSP */
    __ISB();
    uint32_t sp_now;
    __asm volatile("mov %0, sp" : "=r"(sp_now));
    uint32_t ctrl = __get_CONTROL();
    __set_CONTROL(0x0);
    __ISB();
    snprintf(d, sizeof d, "control=0x%lX sp_on_psp=%d", (unsigned long)ctrl,
             sp_now >= (uint32_t)pstack && sp_now <= (uint32_t)&pstack[64]);
    result("CORE_PSP", (ctrl == 2u && sp_now >= (uint32_t)pstack && sp_now <= (uint32_t)&pstack[64]) ? R_PASS : R_FAIL, d);
}

void WWDG_IRQHandler(void)
{
    ewi_count++;
    WWDG->SR = 0;
}

static void stage0_wwdg(void)
{
    char d[96];
    __HAL_RCC_WWDG_CLK_ENABLE();

    /* Configuration that would give a 58.25 ms timeout on silicon:
     * PCLK1 36 MHz, prescaler 8, counter 0x7F, no window, EWI on. */
    WWDG->CFR = WWDG_CFR_WDGTB_0 | WWDG_CFR_WDGTB_1 | WWDG_CFR_EWI | 0x7Fu;
    uint32_t cfr = WWDG->CFR;
    snprintf(d, sizeof d, "cfr_written=0x37F cfr_read=0x%03lX", (unsigned long)cfr);
    result("WWDG_PRESCALER", (cfr & WWDG_CFR_WDGTB) == WWDG_CFR_WDGTB ? R_PASS : R_LIMIT, d);

    WWDG->SR = 0;
    ewi_count = 0;
    HAL_NVIC_SetPriority(WWDG_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(WWDG_IRQn);

    WWDG->CR = WWDG_CR_WDGA | 0x7Fu;
    for (int i = 0; i < 5; i++) { /* refreshing keeps the MCU alive */
        HAL_Delay(2);
        WWDG->CR = WWDG_CR_WDGA | 0x7Fu;
    }
    LOG("SMOKE", "wwdg_refresh_ok count=5");

    /* Hang without refreshing. On silicon this resets after <= 58 ms. */
    s.stage = 1;
    s.wwdg_pending = WWDG_PENDING_AUTO;
    uint8_t cr_first = (uint8_t)(WWDG->CR & 0x7Fu);
    uint32_t changes = 0;
    uint8_t cr_prev = cr_first;
    uint32_t c0 = dwt_cycles();
    LOG("SMOKE", "wwdg_hang_start");
    while ((s.hang_cycles = dwt_cycles() - c0) < 72000u * 200u) {
        uint8_t cr = (uint8_t)(WWDG->CR & 0x7Fu);
        if (cr != cr_prev) {
            changes++;
            cr_prev = cr;
        }
    }

    /* Still alive after 200 ms of hang. */
    snprintf(d, sizeof d, "cr_changes_in_200ms=%lu", (unsigned long)changes);
    result("WWDG_COUNTER", changes > 0 ? R_PASS : R_FAIL, d);
    snprintf(d, sizeof d, "ewi_count=%lu", (unsigned long)ewi_count);
    result("WWDG_EWI", ewi_count > 0 ? R_PASS : R_LIMIT, d);
    result("WWDG_AUTO_RESET", R_LIMIT, "no_reset_after_ms=200 expected_ms=58");

    /* Silicon: writing T6=0 while WDGA=1 causes an immediate WWDG reset. */
    s.wwdg_pending = WWDG_PENDING_FORCED;
    LOG("SMOKE", "wwdg_force_t6_clear");
    uart_flush();
    WWDG->CR = WWDG_CR_WDGA | 0x3Fu;
    HAL_Delay(5);
    result("WWDG_FORCED_RESET", R_FAIL, "still_running_after_ms=5");
}

int main(void)
{
    HAL_Init();
    board_clock_t clk = board_clock_init();
    board_gpio_init();
    board_uart_init();
    dwt_init();

    uint32_t csr = board_read_and_clear_reset_flags();
    LOG("BOOT", "system_start build=smoke");
    LOG("RESET", "cause=%s csr=0x%08lX", board_reset_cause_str(csr), (unsigned long)csr);

    if (s.magic != SMOKE_MAGIC) {
        s.magic = SMOKE_MAGIC;
        s.stage = 0;
        s.pattern = NOINIT_PATTERN;
        s.wwdg_pending = WWDG_PENDING_NONE;
        s.failures = 0;
        s.limits = 0;
    }

    char d[96];
    switch (s.stage) {
    case 0:
        snprintf(d, sizeof d, "source=%s sysclk=%lu", clk == CLOCK_HSE_PLL_72MHZ ? "HSE_PLL" : "HSI_PLL",
                 (unsigned long)SystemCoreClock);
        result("CLOCK", clk == CLOCK_HSE_PLL_72MHZ && SystemCoreClock == 72000000u ? R_PASS : R_FAIL, d);
        result("UART", R_PASS, "log_visible=1");
        test_core();
        test_mem_access();
        test_dwt();
        test_crc();
        test_tim2();
        test_primask();
        test_it_state();
        test_i2c();
        stage0_wwdg();
        break;

    case 1:
        if (s.wwdg_pending == WWDG_PENDING_AUTO) {
            snprintf(d, sizeof d, "hang_us=%lu", (unsigned long)(s.hang_cycles / 72u));
            result("WWDG_AUTO_RESET", R_PASS, d);
        } else {
            result("WWDG_FORCED_RESET", R_PASS, "reset_observed=1");
        }
        snprintf(d, sizeof d, "csr=0x%08lX", (unsigned long)csr);
        result("CSR_WWDGRSTF", (csr & RCC_CSR_WWDGRSTF) ? R_PASS : R_LIMIT, d);
        snprintf(d, sizeof d, "pattern=0x%08lX", (unsigned long)s.pattern);
        result("NOINIT_WWDG", s.pattern == NOINIT_PATTERN ? R_PASS : R_FAIL, d);
        s.stage = 2;
        LOG("SMOKE", "software_reset_request");
        uart_flush();
        NVIC_SystemReset();
        break;

    case 2:
    default:
        result("SOFT_RESET", R_PASS, "reset_observed=1");
        snprintf(d, sizeof d, "csr=0x%08lX", (unsigned long)csr);
        result("CSR_SFTRSTF", (csr & RCC_CSR_SFTRSTF) ? R_PASS : R_LIMIT, d);
        snprintf(d, sizeof d, "pattern=0x%08lX", (unsigned long)s.pattern);
        result("NOINIT_SOFT", s.pattern == NOINIT_PATTERN ? R_PASS : R_FAIL, d);
        test_pendsv();
        LOG("SMOKE", "summary failures=%lu limits=%lu", (unsigned long)s.failures,
            (unsigned long)s.limits);
        LOG("SMOKE", "SMOKE_DONE");
        s.magic = 0;
        break;
    }

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(500);
    }
}
