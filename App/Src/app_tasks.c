#include "app.h"
#include "board.h"
#include "dwt.h"
#include "log.h"
#include "sensor.h"
#include "fault_fw.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

app_config_t g_config = APP_CONFIG_DEFAULT;
app_state_t  g_state;

static QueueHandle_t sensor_q; /* sensor -> control */

static void sensor_task(void *arg)
{
    LOG("TASK", "name=sensor state=started");
    TickType_t last = xTaskGetTickCount();
    uint32_t seq = 0;
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SENSOR_PERIOD_MS));

        app_record_t rec = {.seq = ++seq, .sensor_cyc = dwt_cycles()};
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
        /* Baseline behaviour: on a failed read the control loop keeps
         * consuming the last value held in g_state.temp_centi. */
        rec.temp_centi = g_state.temp_centi;
        rec.status = (uint8_t)st;
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
            fi_site_control(); /* fault-injection point + observation */
            rec.output = control_compute(rec.temp_centi, &g_config);
            g_state.output = rec.output;
            g_state.control_hb++;
            if (!console_post_record(&rec)) {
                g_state.dropped++;
            }
        }
    }
}

void app_start(void)
{
    sensor_q = xQueueCreate(4, sizeof(app_record_t));
    xTaskCreate(sensor_task, "sensor", STACK_SENSOR, NULL, PRIO_SENSOR, NULL);
    xTaskCreate(control_task, "control", STACK_CONTROL, NULL, PRIO_CONTROL, NULL);
    xTaskCreate(console_task, "console", STACK_CONSOLE, NULL, PRIO_CONSOLE, NULL);
}
