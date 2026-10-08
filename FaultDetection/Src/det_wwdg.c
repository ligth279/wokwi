/* Window watchdog supervision of the monitor task.
 *
 * The WWDG is refreshed by the 1 kHz TIM2 interrupt only while the monitor
 * task has shown progress within DET_WD_STALE_MS (det_wd_alive()). A task that
 * spins at a higher priority than the monitor (TIM-01) starves it; the token
 * goes stale, the refresh stops and the WWDG counter runs into T6 = 0.
 *
 * Wokwi does not reset the MCU when that happens (docs/SIMULATOR_LIMITATIONS.md
 * section 1), so the same ISR notices T6 = 0, reports the detection, leaves a
 * breadcrumb and writes CR = WDGA | 0x3F, which does reset the simulated MCU.
 * On silicon the hardware reset would come first and this ISR never acts. */
#include "detect.h"

#if PROTECTED

#include "board.h"
#include "dwt.h"
#include "log.h"

volatile uint32_t det_wd_refreshes;
static volatile uint32_t token_cyc;
static volatile uint8_t  armed, fired;

#define WD_CR_T6  0x40u
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
    /* Enable with counter 0x7F. A cold-boot write right after the clock enable was observed to be
     * ignored in Wokwi (CR read back 0x80), so verify and repeat. */
    for (int i = 0; i < 10; i++) {
        WWDG->CR = WWDG_CR_WDGA | 0x7Fu;
        if ((WWDG->CR & 0x7Fu) >= 0x70u) {
            break;
        }
    }

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

void TIM2_IRQHandler(void)
{
    if (!(TIM2->SR & TIM_SR_UIF)) {
        return;
    }
    TIM2->SR = ~TIM_SR_UIF;
    if (!armed || fired) {
        return;
    }
    uint32_t cyc = dwt_cycles();
    if (!(WWDG->CR & WD_CR_T6)) {
        fired = 1;
        det_report(DET_M_WWDG, "counter_cr=0x%02lX token_age_ms=%lu refreshes=%lu action=reset", (unsigned long)WWDG->CR,
                   (unsigned long)((cyc - token_cyc) / CYC_PER_MS), (unsigned long)det_wd_refreshes);
        det_crumb_save(DET_CRUMB_WWDG, dwt_cycles());
        WWDG->CR = WWDG_CR_WDGA | 0x3Fu; /* forces the reset the simulator's WWDG does not generate */
        for (;;) {
        }
    }
    if (det_wd_token_fresh(cyc, token_cyc, DET_WD_STALE_MS * CYC_PER_MS)) {
        WWDG->CR = WWDG_CR_WDGA | 0x7Fu;
        det_wd_refreshes++;
    }
}

#endif /* PROTECTED */
