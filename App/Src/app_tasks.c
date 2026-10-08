#include "app.h"
#include "board.h"
#include "dwt.h"
#include "log.h"
#include "sensor.h"
#include "fault_fw.h"
#include "fault_study.h"
#if PROTECTED
#include "detect.h"
#endif
#if RECOVERY
#include "recovery.h"
#endif

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

app_config_t g_config = APP_CONFIG_DEFAULT;
app_state_t  g_state;
void *g_task_sensor, *g_task_control, *g_task_console;

static QueueHandle_t sensor_q; /* sensor -> control */
#if RECOVERY
static volatile uint32_t sensor_seq; /* survives a restart of the sensor task, so SENSOR seq stays continuous */
#endif

static void sensor_task(void *arg)
{
    LOG("TASK", "name=sensor state=started");
    TickType_t last = xTaskGetTickCount();
#if !RECOVERY
    uint32_t seq = 0;
#endif
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
        fi_study_site_sensor(); /* fault-injection point (TIM-02) */

#if RECOVERY
        app_record_t rec = {.seq = ++sensor_seq, .sensor_cyc = dwt_cycles()};
#else
        app_record_t rec = {.seq = ++seq, .sensor_cyc = dwt_cycles()};
#endif
        int16_t t;
        sensor_status_t st = sensor_read_temp(&t);
        if (st == SENSOR_OK) {
            g_state.temp_centi = t;
            uint8_t s = 0;
            st = sensor_read_sample(&s);
            rec.chip_sample = s;
        }
        if (st != SENSOR_OK) {
            g_state.sensor_errors++;
        }
#if RECOVERY
        {
            /* An I2C error can leave the peripheral in a state where later reads return shifted data with
             * status OK (seen after a BUS_ERROR next to serial input). Re-initialise it after the first error
             * of a streak; repeating it on every failure of a stuck bus would make the task overrun its period. */
            static uint32_t err_streak;
            if (st == SENSOR_OK) {
                err_streak = 0;
            } else if (err_streak++ == 0) {
                HAL_I2C_DeInit(&hi2c1);
                I2C1->CR1 |= I2C_CR1_SWRST;
                I2C1->CR1 &= ~I2C_CR1_SWRST;
                board_i2c_init();
            }
        }
#endif
#if PROTECTED
        det_i2c_status(st == SENSOR_OK, sensor_status_str(st));
#endif
        /* Baseline behaviour: on a failed read the control loop keeps
         * consuming the last value held in g_state.temp_centi. */
        rec.temp_centi = g_state.temp_centi;
        rec.status = (uint8_t)st;
#if PROTECTED
        rec.crc = det_sample_crc(rec.seq, rec.temp_centi);
#endif
        g_state.sensor_hb++;
        xQueueSend(sensor_q, &rec, 0);
    }
}

static void control_task(void *arg)
{
    LOG("TASK", "name=control state=started");
    app_record_t rec;
    for (;;) {
        if (xQueueReceive(sensor_q, &rec, portMAX_DELAY) == pdTRUE) {
            g_state.sensor_in = rec.temp_centi;
            fi_site_control(); /* fault-injection point + observation */
            rec.input_centi = fi_study_sensor_hook(rec.temp_centi); /* DATA-01 */
#if RECOVERY
            int cfg_ok = det_check_config();
            int smp_ok = det_check_sample(rec.seq, rec.input_centi, rec.crc, rec.temp_centi);
            rec_inline(&rec.input_centi, rec.temp_centi); /* level 2 actions run here, before the control law */
#elif PROTECTED
            det_check_config();
            det_check_sample(rec.seq, rec.input_centi, rec.crc, rec.temp_centi);
#endif
            rec.output = control_compute(rec.input_centi, &g_config);
            g_state.last_input = rec.input_centi;
            g_state.output = rec.output;
#if RECOVERY
            if (cfg_ok && smp_ok) {
                rec_note_input(rec.input_centi);
            }
            rec_control_done(rec.input_centi, rec.output, det_config_ok());
#endif
            g_state.control_hb++;
            if (!console_post_record(&rec)) {
                g_state.dropped++;
            }
        }
    }
}

#if RECOVERY
/* Level 1 recovery: replace one task by a fresh instance (new stack); the others keep running. */
void app_restart_task(int which)
{
    static const struct {
        TaskFunction_t fn;
        const char    *name;
        uint16_t       stack;
        UBaseType_t    prio;
        void         **handle;
    } t[3] = {
        {sensor_task, "sensor", STACK_SENSOR, PRIO_SENSOR, &g_task_sensor},
        {control_task, "control", STACK_CONTROL, PRIO_CONTROL, &g_task_control},
        {console_task, "console", STACK_CONSOLE, PRIO_CONSOLE, &g_task_console},
    };
    vTaskDelete((TaskHandle_t)*t[which].handle);
    xTaskCreate(t[which].fn, t[which].name, t[which].stack, NULL, t[which].prio, (TaskHandle_t *)t[which].handle);
}
#endif

void app_start(void)
{
    sensor_q = xQueueCreate(4, sizeof(app_record_t));
    xTaskCreate(sensor_task, "sensor", STACK_SENSOR, NULL, PRIO_SENSOR, (TaskHandle_t *)&g_task_sensor);
    xTaskCreate(control_task, "control", STACK_CONTROL, NULL, PRIO_CONTROL, (TaskHandle_t *)&g_task_control);
    xTaskCreate(console_task, "console", STACK_CONSOLE, NULL, PRIO_CONSOLE, (TaskHandle_t *)&g_task_console);
}
