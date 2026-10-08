/* Window watchdog supervision of the monitor task.
 *
 * The WWDG is refreshed by the 1 kHz TIM2 interrupt only while the monitor
 * task has shown progress within DET_WD_STALE_MS (det_wd_alive()). A task that
 * spins at a higher priority than the monitor (TIM-01) starves it; the token
 * goes stale and the refresh stops. On silicon the WWDG counter then reaches
 * T6 = 0 and the hardware resets the MCU.
 *
 * Wokwi cannot be used to observe that: its WWDG never resets by itself, ignores
 * the prescaler, and (measured in Step 5, results/raw/step5/probes) does not
 * reload the counter on a refresh write after the first one, so T6 clears
 * about 8 ms after start whatever the firmware does. The shim therefore
 * evaluates the watchdog condition from the progress token: once the refresh has
 * been withheld for DET_WD_SIM_TIMEOUT_MS it reports the detection, leaves a
 * breadcrumb and writes CR = WDGA | 0x3F, the one write that does reset the
 * simulated MCU. Results that rely on this are simulator-workaround results.
 * On silicon the hardware reset happens first and this ISR never acts. */
#include "detect.h"

#if PROTECTED

#include "board.h"
#include "dwt.h"
#include "log.h"

volatile uint32_t det_wd_refreshes;
static volatile uint32_t token_cyc;
static volatile uint8_t  armed, fired;

#define CYC_PER_MS 72000u

void det_wd_alive(void)
{
    token_cyc = dwt_cycles();
}

void det_wwdg_start(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_WWDGEN | RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR; /* let the clock enable take effect before the first write (as the HAL does) */
    token_cyc = dwt_cycles();
    WWDG->CFR = (3u << 7) | 0x7Fu;       /* prescaler /8, window = counter max: refresh allowed any time */
    WWDG->CR = WWDG_CR_WDGA | 0x7Fu;     /* enable, counter = 0x7F */
    TIM2->CR1 = 0;
    TIM2->PSC = 7200u - 1u;              /* 72 MHz / 7200 = 10 kHz */
    TIM2->ARR = 10u - 1u;                /* 1 kHz */
    TIM2->CNT = 0;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->DIER = TIM_DIER_UIE;
    NVIC_SetPriority(TIM2_IRQn, 1);
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 = TIM_CR1_CEN;
    armed = 1;
    LOG("DET", "wwdg started cfr=0x%02lX cr=0x%02lX stale_ms=%u refresh=tim2_1khz", (unsigned long)WWDG->CFR,
        (unsigned long)WWDG->CR, (unsigned)DET_WD_STALE_MS);
}

/* The reset is issued from thread mode, never from inside the ISR: a reset written from
 * handler mode was observed to leave Wokwi's core inside the exception (after the reboot SysTick
 * never fired again and no task ran). The ISR therefore returns into this function through a
 * synthetic exception frame built on MSP (see TIM2_IRQHandler), which also works when the
 * interrupted task's PSP is unusable (CPU-02). */
void __attribute__((noreturn, used)) det_reset_thread(void)
{
    WWDG->CR = WWDG_CR_WDGA | 0x3Fu; /* forces the reset the simulator's WWDG does not generate */
    for (;;) {
    }
}

/* Returns 1 when the (simulated) watchdog has timed out and a reset must be issued. */
int __attribute__((used)) det_tim2_c(void)
{
    if (!(TIM2->SR & TIM_SR_UIF)) {
        return 0;
    }
    TIM2->SR = ~TIM_SR_UIF;
    if (!armed || fired) {
        return 0;
    }
    uint32_t cyc = dwt_cycles();
    if (det_wd_token_fresh(cyc, token_cyc, DET_WD_STALE_MS * CYC_PER_MS)) {
        WWDG->CR = WWDG_CR_WDGA | 0x7Fu; /* refresh (what silicon needs) */
        det_wd_refreshes++;
        return 0;
    }
    /* refresh withheld; has the (simulated) watchdog timeout elapsed? */
    if (!det_wd_token_fresh(cyc, token_cyc, (DET_WD_STALE_MS + DET_WD_SIM_TIMEOUT_MS) * CYC_PER_MS)) {
        fired = 1;
        det_report(DET_M_WWDG, "token_age_ms=%lu refreshes=%lu cr=0x%02lX action=reset",
                   (unsigned long)((cyc - token_cyc) / CYC_PER_MS), (unsigned long)det_wd_refreshes,
                   (unsigned long)WWDG->CR);
        det_crumb_save(DET_CRUMB_WWDG, dwt_cycles());
        return 1;
    }
    return 0;
}

/* ARMv6-M instructions only. On a reset request: build an exception frame on MSP whose PC is
 * det_reset_thread, then return from the exception to thread mode on MSP (EXC_RETURN 0xFFFFFFF9). */
__attribute__((naked)) void TIM2_IRQHandler(void)
{
    __asm volatile("push {r4, lr}\n\t"
                   "bl det_tim2_c\n\t"
                   "cmp r0, #0\n\t"
                   "beq 2f\n\t"
                   "sub sp, sp, #32\n\t"
                   "mov r0, sp\n\t"
                   "movs r3, #0\n\t"
                   "str r3, [r0, #0]\n\t"
                   "str r3, [r0, #4]\n\t"
                   "str r3, [r0, #8]\n\t"
                   "str r3, [r0, #12]\n\t"
                   "str r3, [r0, #16]\n\t"
                   "str r3, [r0, #20]\n\t"
                   "ldr r1, =det_reset_thread\n\t"
                   "str r1, [r0, #24]\n\t"
                   "movs r2, #1\n\t"
                   "lsls r2, r2, #24\n\t"
                   "str r2, [r0, #28]\n\t"
                   "ldr r0, =0xFFFFFFF9\n\t"
                   "bx r0\n\t"
                   "2: pop {r4, pc}\n\t"
                   ".ltorg\n\t");
}

#endif /* PROTECTED */
