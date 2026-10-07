#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>

/* Driver for the custom `temp-sensor` Wokwi chip (chips/temp-sensor.chip.c). */

#define SENSOR_I2C_ADDR    0x48u
#define SENSOR_REG_TEMP    0x00u
#define SENSOR_REG_SAMPLE  0x01u
#define SENSOR_REG_WHOAMI  0x0Fu
#define SENSOR_WHOAMI_VAL  0x5Au
#define SENSOR_TIMEOUT_MS  10u

typedef enum {
    SENSOR_OK = 0,
    SENSOR_ERR_BUSY,     /* bus BUSY flag never cleared (e.g. SDA held low) */
    SENSOR_ERR_TIMEOUT,  /* transfer started but did not complete in time */
    SENSOR_ERR_NACK,     /* device did not acknowledge */
    SENSOR_ERR_BUS,      /* other I2C error (arbitration lost, bus error) */
    SENSOR_ERR_ID,       /* WHO_AM_I mismatch */
} sensor_status_t;

sensor_status_t sensor_read_whoami(uint8_t *id);
sensor_status_t sensor_read_temp(int16_t *centi_c);
sensor_status_t sensor_read_sample(uint8_t *sample);
const char *sensor_status_str(sensor_status_t st);

#endif /* SENSOR_H */
