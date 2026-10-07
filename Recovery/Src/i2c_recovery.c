#include "i2c_recovery.h"
#include "board.h"
#include "dwt.h"

#define SCL_PIN GPIO_PIN_6
#define SDA_PIN GPIO_PIN_7

static void delay_us(uint32_t us)
{
    uint32_t start = dwt_cycles();
    uint32_t ticks = us * (SystemCoreClock / 1000000u);
    while (dwt_cycles() - start < ticks) {
    }
}

static void sda_mode(uint32_t mode)
{
    GPIO_InitTypeDef g = {0};
    g.Pin   = SDA_PIN;
    g.Mode  = mode;
    g.Pull  = mode == GPIO_MODE_INPUT ? GPIO_PULLUP : GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
}

static bool sda_raw_low(void)
{
    return (GPIOB->IDR & SDA_PIN) == 0u;
}

/* In Wokwi, IDR does not reflect the net level while the pin is in AF
 * open-drain mode (an idle bus reads 0), so the line is sampled by briefly
 * switching SDA to input-with-pull-up and back to AF. Only call this while
 * no I2C transfer is in progress. */
bool i2c_sda_is_low(void)
{
    uint32_t crl = GPIOB->CRL;
    sda_mode(GPIO_MODE_INPUT);
    for (volatile int i = 0; i < 20; i++) {
    }
    bool low = sda_raw_low();
    GPIOB->CRL = crl;
    return low;
}

bool i2c_bus_recover(i2c_recovery_report_t *rep)
{
    rep->sda_low_before = i2c_sda_is_low(); /* pin still in AF mode here */
    rep->clocks_sent    = 0;

    HAL_I2C_DeInit(&hi2c1);

    /* SCL: open-drain output, released. SDA: input with pull-up while
     * clocking (the master must not drive SDA during a bus clear). The
     * internal pull-up is used for sensing because the Wokwi resistor part
     * does not pull a released open-drain net high (see
     * docs/SIMULATOR_LIMITATIONS.md); on hardware the external pull-ups
     * give the same reading. */
    GPIO_InitTypeDef g = {0};
    HAL_GPIO_WritePin(GPIOB, SCL_PIN | SDA_PIN, GPIO_PIN_SET);
    g.Pin   = SCL_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_OD;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
    sda_mode(GPIO_MODE_INPUT);
    delay_us(5);

    /* Clock SCL (~100 kHz) until the slave releases SDA. */
    while (sda_raw_low() && rep->clocks_sent < I2C_RECOVERY_MAX_CLOCKS) {
        HAL_GPIO_WritePin(GPIOB, SCL_PIN, GPIO_PIN_RESET);
        delay_us(5);
        HAL_GPIO_WritePin(GPIOB, SCL_PIN, GPIO_PIN_SET);
        delay_us(5);
        rep->clocks_sent++;
    }

    /* STOP condition: SDA low -> released while SCL is high. */
    HAL_GPIO_WritePin(GPIOB, SCL_PIN, GPIO_PIN_RESET);
    delay_us(5);
    HAL_GPIO_WritePin(GPIOB, SDA_PIN, GPIO_PIN_RESET);
    sda_mode(GPIO_MODE_OUTPUT_OD);
    delay_us(5);
    HAL_GPIO_WritePin(GPIOB, SCL_PIN, GPIO_PIN_SET);
    delay_us(5);
    sda_mode(GPIO_MODE_INPUT);
    delay_us(5);
    rep->sda_high_after = !sda_raw_low();

    /* Clear any latched BUSY state inside the I2C peripheral, then re-init. */
    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;
    rep->reinit_ok = board_i2c_init() == HAL_OK;

    return rep->sda_high_after && rep->reinit_ok;
}
