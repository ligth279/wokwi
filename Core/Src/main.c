/*
 * Application entry point (baseline / protected builds).
 * PROTECTED=0 : baseline - no fault detection or recovery.
 */
#include "app.h"
#include "board.h"
#include "dwt.h"
#include "log.h"
#include "fault_fw.h"

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

    if (board_i2c_init() != HAL_OK) {
        LOG("BOOT", "i2c_init=FAIL");
    }

    fi_boot();
    log_rtos_init();
    console_rx_init();
    app_start();

    LOG("RTOS", "scheduler_start heap_free=%u", (unsigned)xPortGetFreeHeapSize());
    vTaskStartScheduler();

    LOG("RTOS", "scheduler_exit");  /* only reached if the idle task could not be created */
    for (;;) {
    }
}
