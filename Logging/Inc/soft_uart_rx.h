#ifndef SOFT_UART_RX_H
#define SOFT_UART_RX_H

#include <stdint.h>

/* Software UART receiver on PA10 (8N1).
 *
 * Why: the Wokwi STM32 USART model does not decode bits arriving on its RX
 * pin (RXNE never sets, at any baud), although the serial-monitor bits do
 * arrive on PA10 - at 9600 baud regardless of the MCU's USART setting
 * (measured: ~7500 cycles/bit at 72 MHz). See docs/SIMULATOR_LIMITATIONS.md.
 *
 * How: EXTI10 falling edge (start bit) -> TIM3 runs at half-bit period;
 * the start bit is verified at 0.5 bit, data bits are sampled at the middle
 * of each bit (1.5 .. 8.5 bit times), stop bit at 9.5. Then EXTI is
 * re-armed. USART1 TX on PA9 is unaffected.
 *
 * ISRs here make no FreeRTOS calls (port rule). */

#define SOFT_UART_RX_BAUD 9600u

typedef struct {
    uint32_t bytes;
    uint32_t framing_errors; /* stop bit read as 0            */
    uint32_t false_starts;   /* start bit not low at mid-bit  */
    uint32_t overflows;      /* ring buffer full, byte dropped */
} soft_uart_stats_t;

void soft_uart_rx_init(void);
int  soft_uart_getc(void); /* -1 if no byte available */
void soft_uart_get_stats(soft_uart_stats_t *out);

#endif /* SOFT_UART_RX_H */
