#ifndef DWT_H
#define DWT_H

#include "stm32f1xx.h"

/* DWT cycle counter (Cortex-M3). Primary timing unit of the study.
 * CYCCNT is 32-bit and wraps after 2^32 / 72 MHz ~= 59.6 s; deltas computed
 * with unsigned subtraction are correct across a single wrap. */

static inline void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t dwt_cycles(void)
{
    return DWT->CYCCNT;
}

#endif /* DWT_H */
