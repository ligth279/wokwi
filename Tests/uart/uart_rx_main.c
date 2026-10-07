/* UART RX probe: receives serial-monitor input through the software UART
 * receiver (Logging/Src/soft_uart_rx.c) and echoes every byte as hex.
 * Expected with Tests/uart/uart_rx.yaml: 41 42 0A 43 0A, 0 errors. */
#include "board.h"
#include "log.h"
#include "soft_uart_rx.h"

int main(void)
{
    HAL_Init();
    board_clock_init();
    board_gpio_init();
    board_uart_init();
    soft_uart_rx_init();
    LOG("BOOT", "system_start build=uartrx");
    LOG("UARTRX", "ready");
    uint32_t last = HAL_GetTick();
    for (;;) {
        int c = soft_uart_getc();
        if (c >= 0) {
            LOG("UARTRX", "byte=0x%02X", c);
        }
        if (HAL_GetTick() - last >= 1000u) {
            last = HAL_GetTick();
            soft_uart_stats_t st;
            soft_uart_get_stats(&st);
            LOG("UARTRX", "stats t=%lu bytes=%lu framing_err=%lu false_start=%lu overflow=%lu",
                (unsigned long)last, (unsigned long)st.bytes, (unsigned long)st.framing_errors,
                (unsigned long)st.false_starts, (unsigned long)st.overflows);
        }
    }
}
