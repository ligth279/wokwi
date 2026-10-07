/*
 * temp-sensor: deterministic I2C temperature sensor for the fault study.
 *
 * I2C address 0x48. Register map (pointer register written first, then read):
 *   0x00 TEMP     2 bytes, big-endian int16, centi-degrees Celsius
 *   0x01 SAMPLE   1 byte, increments on every TEMP read (freshness check)
 *   0x0F WHO_AM_I 1 byte, constant 0x5A
 *
 * TEMP is a deterministic function of simulation time so runs are
 * reproducible: base + triangle wave of +-3.00 C with a 20 s period.
 * "base" comes from the `temperature` attribute (default 25.0 C).
 */
#include "wokwi-api.h"
#include <stdint.h>
#include <stdlib.h>

#define SENSOR_ADDR 0x48
#define REG_TEMP    0x00
#define REG_SAMPLE  0x01
#define REG_WHOAMI  0x0F
#define WHOAMI_VAL  0x5A

typedef struct {
    uint32_t temp_attr;
    uint8_t  reg;
    bool     expect_reg; /* next written byte is the register pointer */
    uint8_t  buf[2];
    uint8_t  len;
    uint8_t  idx;
    uint8_t  sample;
} chip_state_t;

static int16_t temperature_centi(chip_state_t *chip)
{
    float base = attr_read_float(chip->temp_attr);
    uint64_t ms = get_sim_nanos() / 1000000ull;
    uint32_t phase = (uint32_t)(ms % 20000u);            /* 0..19999 ms */
    int32_t tri = phase < 10000u ? (int32_t)phase : (int32_t)(20000u - phase); /* 0..10000 */
    int32_t offset = (tri * 600) / 10000 - 300;          /* -300..+300 centi-C */
    return (int16_t)((int32_t)(base * 100.0f) + offset);
}

static bool on_connect(void *user_data, uint32_t address, bool read)
{
    chip_state_t *chip = user_data;
    chip->idx = 0;
    if (!read) {
        chip->expect_reg = true;
        return true;
    }
    switch (chip->reg) {
    case REG_TEMP: {
        int16_t t = temperature_centi(chip);
        chip->buf[0] = (uint8_t)((uint16_t)t >> 8);
        chip->buf[1] = (uint8_t)t;
        chip->len = 2;
        chip->sample++;
        break;
    }
    case REG_SAMPLE:
        chip->buf[0] = chip->sample;
        chip->len = 1;
        break;
    case REG_WHOAMI:
        chip->buf[0] = WHOAMI_VAL;
        chip->len = 1;
        break;
    default:
        chip->buf[0] = 0xFF;
        chip->len = 1;
        break;
    }
    return true;
}

static uint8_t on_read(void *user_data)
{
    chip_state_t *chip = user_data;
    uint8_t v = chip->idx < chip->len ? chip->buf[chip->idx] : 0xFF;
    chip->idx++;
    return v;
}

static bool on_write(void *user_data, uint8_t data)
{
    chip_state_t *chip = user_data;
    if (chip->expect_reg) {
        chip->reg = data;
        chip->expect_reg = false;
    }
    return true; /* ACK */
}

static void on_disconnect(void *user_data)
{
}

void chip_init(void)
{
    chip_state_t *chip = calloc(1, sizeof(chip_state_t));
    chip->temp_attr = attr_init_float("temperature", 25.0f);

    const i2c_config_t cfg = {
        .user_data  = chip,
        .address    = SENSOR_ADDR,
        .scl        = pin_init("SCL", INPUT_PULLUP),
        .sda        = pin_init("SDA", INPUT_PULLUP),
        .connect    = on_connect,
        .read       = on_read,
        .write      = on_write,
        .disconnect = on_disconnect,
    };
    i2c_init(&cfg);
}
