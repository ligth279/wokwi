#ifndef I2C_RECOVERY_H
#define I2C_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

/* I2C bus-clear recovery (I2C-bus spec UM10204 §3.1.16): if a slave holds
 * SDA low, clock SCL up to I2C_RECOVERY_MAX_CLOCKS times until SDA is
 * released, issue a STOP, then software-reset and re-initialise I2C1. */

#define I2C_RECOVERY_MAX_CLOCKS 16u

typedef struct {
    bool     sda_low_before;
    uint32_t clocks_sent;
    bool     sda_high_after;
    bool     reinit_ok;
} i2c_recovery_report_t;

bool i2c_sda_is_low(void);
bool i2c_bus_recover(i2c_recovery_report_t *rep);

#endif /* I2C_RECOVERY_H */
