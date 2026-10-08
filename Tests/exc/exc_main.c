/* Step 5 probe: does Wokwi deliver synchronous Cortex-M3 fault exceptions at all?
 * CASE=1 UDF (UsageFault/HardFault), 2 read of unmapped 0x60000000 (BusFault),
 * 3 unaligned word load with CCR.UNALIGN_TRP (UsageFault), 4 divide by zero with CCR.DIV_0_TRP. */
#include "board.h"
#include "dwt.h"
#include "log.h"

#ifndef CASE
#define CASE 1
#endif

static void report(const char *name)
{
    LOG("EXC", "handler=%s cfsr=0x%08lX hfsr=0x%08lX mmfar=0x%08lX bfar=0x%08lX shcsr=0x%08lX", name,
        (unsigned long)SCB->CFSR, (unsigned long)SCB->HFSR, (unsigned long)SCB->MMFAR,
        (unsigned long)SCB->BFAR, (unsigned long)SCB->SHCSR);
    for (;;) {
    }
}
void HardFault_Handler(void) { report("HardFault"); }
void MemManage_Handler(void) { report("MemManage"); }
void BusFault_Handler(void) { report("BusFault"); }
void UsageFault_Handler(void) { report("UsageFault"); }

int main(void)
{
    HAL_Init();
    board_clock_init();
    board_uart_init();
    dwt_init();
    LOG("BOOT", "exc_probe case=%d", CASE);
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
    LOG("EXC", "shcsr_after_enable=0x%08lX ccr=0x%08lX", (unsigned long)SCB->SHCSR, (unsigned long)SCB->CCR);
#if CASE == 3
    SCB->CCR |= SCB_CCR_UNALIGN_TRP_Msk;
#elif CASE == 4
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
#endif
    LOG("EXC", "ccr=0x%08lX injecting", (unsigned long)SCB->CCR);
#if CASE == 1
    __asm volatile(".short 0xDE00"); /* UDF #0 */
#elif CASE == 2
    volatile uint32_t v = *(volatile uint32_t *)0x60000000u;
    LOG("EXC", "read_returned=0x%08lX", (unsigned long)v);
#elif CASE == 3
    static uint8_t buf[8];
    volatile uint32_t v = *(volatile uint32_t *)(buf + 1);
    LOG("EXC", "read_returned=0x%08lX", (unsigned long)v);
#elif CASE == 4
    register uint32_t a = 100, b = 0, q;
    __asm volatile(".arch armv7-m\n sdiv %0, %1, %2" : "=r"(q) : "r"(a), "r"(b));
    LOG("EXC", "div_returned=%lu", (unsigned long)q);
#endif
    LOG("EXC", "no_exception");
    for (;;) {
    }
}
