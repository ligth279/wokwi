/*
 * Console task: the only task that formats periodic output.
 *  - prints every pipeline record as [SENSOR] / [CONTROL] lines
 *  - prints a [STATUS] line every STATUS_PERIOD_MS
 *  - reads newline-terminated commands from the serial monitor through the
 *    software UART receiver (Logging/Src/soft_uart_rx.c)
 */
#include "app.h"
#include "board.h"
#include "log.h"
#include "sensor.h"
#include "soft_uart_rx.h"
#include "fault_cmd.h"
#include "fault_inject.h"
#include "fault_fw.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define CMD_MAX      48u

static uint32_t fault_cmd_ok, fault_cmd_rej;

static QueueHandle_t log_q; /* control -> console */
void console_rx_init(void)
{
    log_q = xQueueCreate(8, sizeof(app_record_t));
    soft_uart_rx_init();
}

int console_post_record(const app_record_t *rec)
{
    return xQueueSend(log_q, rec, 0) == pdTRUE;
}

static void print_status(const char *src)
{
    soft_uart_stats_t rx;
    soft_uart_get_stats(&rx);
    LOG("STATUS", "src=%s t_ms=%lu sensor_hb=%lu control_hb=%lu console_hb=%lu sensor_err=%lu dropped=%lu heap_free=%u rx_bytes=%lu rx_err=%lu fault_cmd_ok=%lu fault_cmd_rej=%lu faults_injected=%lu fi_active=%s",
        src, (unsigned long)xTaskGetTickCount(), (unsigned long)g_state.sensor_hb,
        (unsigned long)g_state.control_hb, (unsigned long)g_state.console_hb,
        (unsigned long)g_state.sensor_errors, (unsigned long)g_state.dropped,
        (unsigned)xPortGetFreeHeapSize(), (unsigned long)rx.bytes,
        (unsigned long)(rx.framing_errors + rx.false_starts + rx.overflows), (unsigned long)fault_cmd_ok,
        (unsigned long)fault_cmd_rej, (unsigned long)g_faults_injected, fi_active_exp());
}

#ifdef DEBUG_STACK
static void print_stack_usage(void)
{
    static const char *const names[] = {"sensor", "control", "console", "IDLE"};
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        TaskHandle_t h = xTaskGetHandle(names[i]);
        if (h != NULL) {
            LOG("DBGSTACK", "task=%s min_free_words=%lu", names[i], (unsigned long)uxTaskGetStackHighWaterMark(h));
        }
    }
}
#endif

/* FAULT <ID> / FAULT_GDB <ID> / FAULT_AT <ID> <ms>: validate, then hand the
 * fault to the framework, which owns the experiment from SELECTED onwards.
 * The command itself never modifies the system. */
static void handle_fault(const char *cmd, const char *args, fi_mech_t mech)
{
    fault_cmd_t fc;
    if (mech == FI_MECH_TIMER) {
        fault_cmd_parse_at(args, &fc);
    } else {
        fault_cmd_parse(args, &fc);
    }
    if (fc.status != FAULT_CMD_OK) {
        fault_cmd_rej++;
        if (fc.id[0] != '\0') {
            LOG("CMD", "result=rejected cmd=%s reason=%s arg=%s", cmd, fault_cmd_status_str(fc.status), fc.id);
        } else {
            LOG("CMD", "result=rejected cmd=%s reason=%s", cmd, fault_cmd_status_str(fc.status));
        }
        return;
    }
    if (fi_select(fc.fault, mech, fc.delay_ms) == FI_SELECT_BUSY) {
        fault_cmd_rej++;
        LOG("CMD", "result=rejected cmd=%s reason=busy active=%s", cmd, fi_active_exp());
        return;
    }
    fault_cmd_ok++;
    LOG("CMD", "result=accepted cmd=%s id=%s class=%s name=%s EXP=%s", cmd, fc.fault->id,
        fault_class_str(fc.fault->cls), fc.fault->name, fi_cur_exp_id);
    fi_log_pending();
}

static void handle_command(char *cmd)
{
    LOG("CMD", "rx=%s", cmd);

    /* split the command word from its arguments */
    char *args = cmd;
    while (*args != '\0' && *args != ' ') args++;
    if (*args == ' ') *args++ = '\0';

    if (strcmp(cmd, "FAULT") == 0) {
        handle_fault(cmd, args, FI_MECH_UART);
    } else if (strcmp(cmd, "FAULT_AT") == 0) {
        handle_fault(cmd, args, FI_MECH_TIMER);
    } else if (strcmp(cmd, "FAULT_GDB") == 0) {
        handle_fault(cmd, args, FI_MECH_GDB);
    } else if (*args != '\0' && (strcmp(cmd, "PING") == 0 || strcmp(cmd, "STATUS") == 0 ||
                                 strcmp(cmd, "HELP") == 0)) {
        LOG("CMD", "result=rejected cmd=%s reason=extra_args", cmd);
    } else if (strcmp(cmd, "PING") == 0) {
        LOG("CMD", "PONG");
    } else if (strcmp(cmd, "STATUS") == 0) {
        print_status("cmd");
    } else if (strcmp(cmd, "HELP") == 0) {
        LOG("CMD", "commands=PING,STATUS,HELP,FAULT_<ID>,FAULT_AT_<ID>_<ms>,FAULT_GDB_<ID>");
        char ids[96];
        size_t n = 0;
        ids[0] = '\0';
        for (unsigned i = 0; i < fault_catalog_count; i++) {
            n += (size_t)snprintf(ids + n, sizeof ids - n, "%s%s", i ? "," : "", fault_catalog[i].id);
        }
        LOG("CMD", "fault_ids=%s", ids);
    } else {
        LOG("CMD", "result=rejected reason=unknown_command");
    }
}

/* Lines are accumulated up to CMD_MAX printable characters. An overlong line
 * or one containing a non-printable byte is rejected as a whole (never
 * truncated and executed). */
static void poll_commands(void)
{
    static char line[CMD_MAX + 1];
    static uint32_t len;
    static uint32_t overflow, bad_char;
    int c;
    while ((c = soft_uart_getc()) >= 0) {
        if (c == '\r' || c == '\n') {
            if (overflow) {
                LOG("CMD", "result=rejected reason=line_too_long len=%lu max=%u", (unsigned long)(len + overflow),
                    (unsigned)CMD_MAX);
            } else if (bad_char) {
                LOG("CMD", "result=rejected reason=non_printable_char");
            } else if (len > 0) {
                line[len] = '\0';
                handle_command(line);
            }
            len = overflow = bad_char = 0;
        } else if (c < 0x20 || c > 0x7E) {
            bad_char = 1;
        } else if (len < CMD_MAX) {
            line[len++] = (char)c;
        } else {
            overflow++;
        }
    }
}

void console_task(void *arg)
{
    LOG("TASK", "name=console state=started");
    LOG("APP", "system_ready");
    TickType_t next_status = xTaskGetTickCount() + pdMS_TO_TICKS(STATUS_PERIOD_MS);
    app_record_t rec;
    for (;;) {
        if (xQueueReceive(log_q, &rec, pdMS_TO_TICKS(10)) == pdTRUE) {
            LOG("SENSOR", "seq=%lu value=%d sample=%u status=%s cyc=%lu", (unsigned long)rec.seq,
                rec.temp_centi, rec.chip_sample, sensor_status_str((sensor_status_t)rec.status),
                (unsigned long)rec.sensor_cyc);
            LOG("CONTROL", "seq=%lu value=%d input=%d", (unsigned long)rec.seq, rec.output,
                rec.temp_centi);
        }
        poll_commands();
        const char *gdb_req = fi_take_gdb_request();
        if (gdb_req != NULL) {
            LOG("CMD", "rx_gdb=%s", gdb_req);
            handle_fault("GDB_REQUEST", gdb_req, FI_MECH_GDB);
        }
        fi_log_pending();
        if ((int32_t)(xTaskGetTickCount() - next_status) >= 0) {
            next_status += pdMS_TO_TICKS(STATUS_PERIOD_MS);
            print_status("periodic");
#ifdef DEBUG_STACK
            print_stack_usage();
#endif
        }
        g_state.console_hb++;
    }
}
