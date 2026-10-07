/*
 * i2c-stuck: peripheral fault injector for PERIPH-01 ("custom Wokwi chip
 * holds the I2C line low").
 *
 * Shares the SDA/SCL nets with the sensor. Driven by the MCU via TRIG:
 *   TRIG rising edge  -> SDA forced LOW (stuck-low fault begins)
 *   TRIG held HIGH    -> SDA stays LOW whatever happens on SCL
 *                        (permanent fault: bus recovery cannot clear it)
 *   TRIG LOW again    -> SDA is released only after 9 SCL rising edges
 *                        (models a slave locked mid-byte, which the standard
 *                        I2C bus-clear procedure of 9 SCL pulses frees)
 */
#include "wokwi-api.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RELEASE_CLOCKS 9

typedef struct {
    pin_t    sda;
    pin_t    scl;
    pin_t    trig;
    bool     stuck;
    uint32_t clocks;
} chip_state_t;

static void release_sda(chip_state_t *chip)
{
    pin_mode(chip->sda, INPUT);
    chip->stuck = false;
    printf("[i2c-stuck] SDA released after %u SCL clocks\n", (unsigned)chip->clocks);
}

static void on_trig(void *user_data, pin_t pin, uint32_t value)
{
    chip_state_t *chip = user_data;
    if (value == HIGH) {
        chip->stuck  = true;
        chip->clocks = 0;
        pin_mode(chip->sda, OUTPUT_LOW);
        printf("[i2c-stuck] SDA held LOW\n");
    } else if (chip->stuck) {
        chip->clocks = 0; /* count bus-clear clocks from the moment TRIG drops */
        printf("[i2c-stuck] TRIG released; waiting for %d SCL clocks\n", RELEASE_CLOCKS);
    }
}

static void on_scl(void *user_data, pin_t pin, uint32_t value)
{
    chip_state_t *chip = user_data;
    if (!chip->stuck || pin_read(chip->trig) == HIGH) {
        return;
    }
    if (++chip->clocks >= RELEASE_CLOCKS) {
        release_sda(chip);
    }
}

void chip_init(void)
{
    chip_state_t *chip = calloc(1, sizeof(chip_state_t));
    chip->sda  = pin_init("SDA", INPUT);
    chip->scl  = pin_init("SCL", INPUT);
    chip->trig = pin_init("TRIG", INPUT_PULLDOWN);

    const pin_watch_config_t trig_watch = {
        .user_data  = chip,
        .edge       = BOTH,
        .pin_change = on_trig,
    };
    pin_watch(chip->trig, &trig_watch);

    const pin_watch_config_t scl_watch = {
        .user_data  = chip,
        .edge       = RISING,
        .pin_change = on_scl,
    };
    pin_watch(chip->scl, &scl_watch);
}
