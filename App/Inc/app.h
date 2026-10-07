#ifndef APP_H
#define APP_H

#include <stdint.h>

/* Normal application: sensor task -> control task -> console (UART) task.
 *
 *   sensor  (prio 3, every SENSOR_PERIOD_MS): reads TEMP + SAMPLE over I2C
 *   control (prio 2, event-driven):           output = f(temp, g_config)
 *   console (prio 1):                         prints records, status, commands
 */

#define SENSOR_PERIOD_MS  100u
#define STATUS_PERIOD_MS  1000u

#define PRIO_SENSOR   3
#define PRIO_CONTROL  2
#define PRIO_CONSOLE  1

#define STACK_SENSOR   384u /* words */
#define STACK_CONTROL  320u
#define STACK_CONSOLE  384u

/* Control configuration (target of DATA-02 configuration corruption). */
typedef struct {
    int16_t setpoint_centi; /* centi-degC                     */
    int16_t kp_pct_per_c;   /* output % per degC of error     */
    int16_t out_min;        /* output clamp, %                */
    int16_t out_max;
} app_config_t;

#define APP_CONFIG_DEFAULT {2200, 15, 0, 100}

/* One sample travelling through the pipeline. */
typedef struct {
    uint32_t seq;
    uint32_t sensor_cyc;  /* DWT cycle count when the sample was taken */
    int16_t  temp_centi;  /* value handed to the control loop          */
    uint8_t  chip_sample; /* sensor chip SAMPLE register               */
    uint8_t  status;      /* sensor_status_t of this read              */
    int16_t  output;      /* control output, %                         */
} app_record_t;

typedef struct {
    volatile uint32_t sensor_hb;   /* per-task progress counters */
    volatile uint32_t control_hb;
    volatile uint32_t console_hb;
    volatile int16_t  temp_centi;  /* last good sensor value (target of DATA-01) */
    volatile int16_t  output;      /* last control output                        */
    volatile uint32_t sensor_errors;
    volatile uint32_t dropped;     /* records lost because the log queue was full */
} app_state_t;

extern app_config_t g_config;
extern app_state_t  g_state;

int16_t control_compute(int16_t temp_centi, const app_config_t *cfg);

void app_start(void);               /* create queues + tasks (before scheduler) */
void console_task(void *arg);
void console_rx_init(void);
int  console_post_record(const app_record_t *rec);

#endif /* APP_H */
