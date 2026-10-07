#include "sensor.h"
#include "board.h"

static sensor_status_t map_status(HAL_StatusTypeDef st)
{
    switch (st) {
    case HAL_OK:      return SENSOR_OK;
    case HAL_BUSY:    return SENSOR_ERR_BUSY;
    case HAL_TIMEOUT: return SENSOR_ERR_TIMEOUT;
    default:
        return (HAL_I2C_GetError(&hi2c1) & HAL_I2C_ERROR_AF) ? SENSOR_ERR_NACK : SENSOR_ERR_BUS;
    }
}

/* Register read = pointer write (with STOP) followed by a separate read.
 * A repeated-START combined transfer (HAL_I2C_Mem_Read) is not used because
 * the Wokwi STM32 I2C model never issues the repeated START (SB stays clear,
 * HAL_I2C_WRONG_START); see docs/SIMULATOR_LIMITATIONS.md. The sensor chip
 * latches the register pointer, so the split transfer is equivalent. */
static sensor_status_t read_reg(uint8_t reg, uint8_t *buf, uint16_t len)
{
    const uint16_t addr = (uint16_t)(SENSOR_I2C_ADDR << 1);
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c1, addr, &reg, 1, SENSOR_TIMEOUT_MS);
    if (st == HAL_OK) {
        st = HAL_I2C_Master_Receive(&hi2c1, addr, buf, len, SENSOR_TIMEOUT_MS);
    }
    return map_status(st);
}

sensor_status_t sensor_read_whoami(uint8_t *id)
{
    sensor_status_t st = read_reg(SENSOR_REG_WHOAMI, id, 1);
    if (st == SENSOR_OK && *id != SENSOR_WHOAMI_VAL) {
        return SENSOR_ERR_ID;
    }
    return st;
}

sensor_status_t sensor_read_temp(int16_t *centi_c)
{
    uint8_t b[2];
    sensor_status_t st = read_reg(SENSOR_REG_TEMP, b, 2);
    if (st == SENSOR_OK) {
        *centi_c = (int16_t)(((uint16_t)b[0] << 8) | b[1]);
    }
    return st;
}

sensor_status_t sensor_read_sample(uint8_t *sample)
{
    return read_reg(SENSOR_REG_SAMPLE, sample, 1);
}

const char *sensor_status_str(sensor_status_t st)
{
    switch (st) {
    case SENSOR_OK:          return "OK";
    case SENSOR_ERR_BUSY:    return "BUSY";
    case SENSOR_ERR_TIMEOUT: return "TIMEOUT";
    case SENSOR_ERR_NACK:    return "NACK";
    case SENSOR_ERR_BUS:     return "BUS_ERROR";
    case SENSOR_ERR_ID:      return "BAD_ID";
    }
    return "?";
}
