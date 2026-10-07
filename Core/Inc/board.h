#ifndef BOARD_H
#define BOARD_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* Blue Pill wiring used by this project (must match diagram.json):
 *   USART1 TX = PA9 -> Wokwi serial monitor, 115200 8N1 (TX only)
 *   PA10 = serial-monitor input, decoded by the software UART receiver
 *          (Logging/Src/soft_uart_rx.c, 9600 8N1) - Wokwi's USART RX
 *          does not decode pin input
 *   I2C1   SCL = PB6, SDA = PB7 -> custom I2C sensor chip
 *   PB0  = TRIG output to the `i2c-stuck` fault chip (PERIPH-01)
 *   PC13 = on-board LED (active low)
 */

typedef enum {
    CLOCK_HSE_PLL_72MHZ = 0, /* intended configuration: 8 MHz HSE x9 */
    CLOCK_HSI_PLL_64MHZ = 1, /* fallback if HSE does not start          */
} board_clock_t;

extern I2C_HandleTypeDef  hi2c1;

#define FAULT_I2C_TRIG_PORT GPIOB
#define FAULT_I2C_TRIG_PIN  GPIO_PIN_0

board_clock_t board_clock_init(void);
void board_gpio_init(void);
void board_uart_init(void);
HAL_StatusTypeDef board_i2c_init(void);

/* Reset-cause register snapshot, read once at boot before flags are cleared. */
uint32_t board_read_and_clear_reset_flags(void);
const char *board_reset_cause_str(uint32_t csr);

#endif /* BOARD_H */
