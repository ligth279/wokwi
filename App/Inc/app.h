#ifndef APP_H
#define APP_H

#include <stdint.h>

#ifndef PROTECTED
#define PROTECTED 0 /* 1 = fault-detection layer compiled in (FaultDetection/) */
#endif

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

#if PROTECTED
/* A detection report formats text (vsnprintf + the 256-byte log line), ~190 words of stack; the
 * sensor and control tasks report from their own context, so they need that much more
 * (measured: CPU-03 reported from the 320-word control stack and overflowed it). */
#define STACK_SENSOR   512u /* words */
#define STACK_CONTROL  512u
#define STACK_CONSOLE  384u
#define STACK_MONITOR  400u /* detection monitor task */
#else
#define STACK_SENSOR   384u /* words */
#define STACK_CONTROL  320u
#define STACK_CONSOLE  384u
#endif
#define PRIO_MONITOR   1    /* same as the console: starved by a spinning control task */

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
    int16_t  temp_centi;  /* value read by the sensor task             */
    int16_t  input_centi; /* value the control loop actually consumed (differs only under DATA-01) */
    uint8_t  chip_sample; /* sensor chip SAMPLE register               */
    uint8_t  status;      /* sensor_status_t of this read              */
    int16_t  output;      /* control output, %                         */
#if PROTECTED
    uint32_t crc;         /* CRC-32 of (seq, temp_centi) taken by the sensor task */
#endif
} app_record_t;

typedef struct {
    volatile uint32_t sensor_hb;   /* per-task progress counters */
    volatile uint32_t control_hb;
    volatile uint32_t console_hb;
    volatile int16_t  temp_centi;  /* last good sensor value (target of DATA-01) */
    volatile int16_t  output;      /* last control output                        */
    volatile int16_t  sensor_in;   /* sample the control task is about to use     */
    volatile int16_t  last_input;  /* input of the last control computation       */
    volatile uint32_t sensor_errors;
    volatile uint32_t dropped;     /* records lost because the log queue was full */
} app_state_t;

extern app_config_t g_config;
extern app_state_t  g_state;

/* FreeRTOS task handles (TaskHandle_t), exposed for the fault injection
 * routines (FaultInjection/Src/fault_study.c). */
extern void *g_task_sensor;
extern void *g_task_control;
extern void *g_task_console;

int16_t control_compute(int16_t temp_centi, const app_config_t *cfg);

void app_start(void);               /* create queues + tasks (before scheduler) */
void console_task(void *arg);
void console_rx_init(void);
int  console_post_record(const app_record_t *rec);

#endif /* APP_H */
