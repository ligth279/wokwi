#include "board.h"

I2C_HandleTypeDef  hi2c1;

board_clock_t board_clock_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* Target: HSE 8 MHz -> PLL x9 = 72 MHz SYSCLK, APB1 = 36 MHz, APB2 = 72 MHz. */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL     = RCC_PLL_MUL9;
    board_clock_t result = CLOCK_HSE_PLL_72MHZ;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        /* Fallback: HSI/2 x16 = 64 MHz (max reachable from HSI on F103). */
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSEState       = RCC_HSE_OFF;
        osc.HSIState       = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.PLL.PLLState   = RCC_PLL_ON;
        osc.PLL.PLLSource  = RCC_PLLSOURCE_HSI_DIV2;
        osc.PLL.PLLMUL     = RCC_PLL_MUL16;
        HAL_RCC_OscConfig(&osc);
        result = CLOCK_HSI_PLL_64MHZ;
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);

    return result;
}

void board_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin   = GPIO_PIN_13;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_GPIO_Init(GPIOC, &g);

    HAL_GPIO_WritePin(FAULT_I2C_TRIG_PORT, FAULT_I2C_TRIG_PIN, GPIO_PIN_RESET);
    g.Pin = FAULT_I2C_TRIG_PIN;
    HAL_GPIO_Init(FAULT_I2C_TRIG_PORT, &g);
}

/* USART1 TX-only, 115200 8N1, configured at register level. HAL_UART is
 * not used: its driver relies on LDREX/STREX, which are not part of the
 * ARMv6-M instruction subset the project is compiled for. */
void board_uart_init(void)
{
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin   = GPIO_PIN_9;
    g.Mode  = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);

    USART1->CR1 = 0;
    USART1->CR2 = 0;
    USART1->CR3 = 0;
    /* USARTDIV = PCLK2 / (16 * baud); BRR = mantissa:fraction(4 bits), rounded. */
    uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
    USART1->BRR = (pclk2 + 115200u / 2u) / 115200u;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE;
}

HAL_StatusTypeDef board_i2c_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
    g.Mode  = GPIO_MODE_AF_OD;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);

    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 100000;
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2     = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    return HAL_I2C_Init(&hi2c1);
}

uint32_t board_read_and_clear_reset_flags(void)
{
    uint32_t csr = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    return csr;
}

/* Highest-priority single cause for logging. Several flags can be set at
 * once (e.g. PINRSTF accompanies internal resets on real silicon), so the
 * raw register value is always logged alongside this string. */
const char *board_reset_cause_str(uint32_t csr)
{
    if (csr & RCC_CSR_WWDGRSTF) return "WWDG";
    if (csr & RCC_CSR_IWDGRSTF) return "IWDG";
    if (csr & RCC_CSR_SFTRSTF)  return "SOFTWARE";
    if (csr & RCC_CSR_LPWRRSTF) return "LOW_POWER";
    if (csr & RCC_CSR_PORRSTF)  return "POWER_ON";
    if (csr & RCC_CSR_PINRSTF)  return "PIN";
    return "NONE";
}
