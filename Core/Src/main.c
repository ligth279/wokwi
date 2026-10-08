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

int main(void)
{
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
