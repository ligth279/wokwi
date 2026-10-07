#include "app.h"

/* Proportional cooling controller: output (fan duty, %) rises with the
 * temperature error above the setpoint and is clamped to [out_min, out_max].
 *   output = clamp(((temp - setpoint) * kp) / 100, out_min, out_max)
 * Integer arithmetic, C truncation toward zero (the log checker replicates
 * this exactly). */
int16_t control_compute(int16_t temp_centi, const app_config_t *cfg)
{
    int32_t err = (int32_t)temp_centi - cfg->setpoint_centi;
    int32_t out = (err * cfg->kp_pct_per_c) / 100;
    if (out < cfg->out_min) out = cfg->out_min;
    if (out > cfg->out_max) out = cfg->out_max;
    return (int16_t)out;
}
