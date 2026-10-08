/*
 * Application entry point (baseline / protected builds).
 * PROTECTED=0 : baseline - no fault detection or recovery.
 * PROTECTED=1 : protected - fault-detection layer (FaultDetection/); detection only, no recovery.
 */
#include "app.h"
#include "board.h"
#include "dwt.h"
#include "log.h"
#include "fault_fw.h"
#if PROTECTED
#include "detect.h"
#endif

#include "FreeRTOS.h"
#include "task.h"

#ifndef PROTECTED
#define PROTECTED 0
#endif

#if PROTECTED
/* A reset forced inside Wokwi (WWDG) restarts the core but leaves the rest of the machine as it
 * was: interrupts enabled in the NVIC (and possibly pending), SysTick running, peripherals
 * configured, CONTROL.SPSEL as the old code left it. Observed without this: the simulation dies
 * (API error 1006) right after the second BOOT line in half of the reset runs. Return the
 * machine to its power-on state before anything else runs. */
static void reset_machine_state(void)
{
    /* If the thread was running on PSP, CONTROL.SPSEL is still 1: move to MSP at the same stack
     * address (this function's frame stays valid) and clear CONTROL. */
    __asm volatile("mrs r0, control\n\t"
                   "movs r1, #2\n\t"
                   "tst r0, r1\n\t"
                   "beq 1f\n\t"
                   "mrs r0, psp\n\t"
                   "msr msp, r0\n\t"
                   "movs r0, #0\n\t"
                   "msr control, r0\n\t"
                   "isb\n\t"
                   "1:\n\t" ::: "r0", "r1", "cc", "memory");
    __asm volatile("msr primask, %0" ::"r"(0u) : "memory");
    NVIC->ICER[0] = 0xFFFFFFFFu;
    NVIC->ICER[1] = 0xFFFFFFFFu;
    NVIC->ICPR[0] = 0xFFFFFFFFu;
    NVIC->ICPR[1] = 0xFFFFFFFFu;
    SysTick->CTRL = 0;
    RCC->APB1RSTR = 0xFFFFFFFFu;
    RCC->APB1RSTR = 0;
    RCC->APB2RSTR = 0xFFFFFFFFu;
    RCC->APB2RSTR = 0;
}
#endif

int main(void)
{
#if PROTECTED
    reset_machine_state();
#endif
    HAL_Init();
    board_clock_t clk = board_clock_init();
    board_gpio_init();
    board_uart_init();
    dwt_init();

    uint32_t csr = board_read_and_clear_reset_flags();
    LOG("BOOT", "system_start build=%s protection=%d sysclk=%lu clock=%s",
        PROTECTED ? "protected" : "baseline", PROTECTED, (unsigned long)SystemCoreClock,
        clk == CLOCK_HSE_PLL_72MHZ ? "HSE_PLL" : "HSI_PLL");
    LOG("RESET", "cause=%s csr=0x%08lX", board_reset_cause_str(csr), (unsigned long)csr);
#if PROTECTED
    det_boot(csr); /* resolve the reset cause (RCC_CSR + breadcrumb) */
#endif

    if (board_i2c_init() != HAL_OK) {
        LOG("BOOT", "i2c_init=FAIL");
    }

    fi_boot();
    log_rtos_init();
#if PROTECTED
    det_init(); /* CRC unit, fault handlers, protected-data copies */
#endif
    console_rx_init();
    app_start();
#if PROTECTED
    det_start(); /* stack guards, monitor task */
    det_wwdg_start();
#endif

    LOG("RTOS", "scheduler_start heap_free=%u", (unsigned)xPortGetFreeHeapSize());
    vTaskStartScheduler();

    LOG("RTOS", "scheduler_exit");  /* only reached if the idle task could not be created */
    for (;;) {
    }
}
