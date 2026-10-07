#include "log.h"
#include "stm32f1xx.h"
#include <stdarg.h>
#include <stdio.h>

#ifdef USE_FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static SemaphoreHandle_t log_mutex;

void log_rtos_init(void)
{
    log_mutex = xSemaphoreCreateMutex();
}

/* Serialise whole lines between tasks. ISRs and fault handlers (IPSR != 0)
 * and pre-scheduler code write directly. */
static int log_lock(void)
{
    if (log_mutex == NULL || __get_IPSR() != 0u ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return 0;
    }
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    return 1;
}

static void log_unlock(int locked)
{
    if (locked) {
        xSemaphoreGive(log_mutex);
    }
}
#else
static int log_lock(void) { return 0; }
static void log_unlock(int locked) { (void)locked; }
#endif

void log_putc(char c)
{
    while (!(USART1->SR & USART_SR_TXE)) {
    }
    USART1->DR = (uint8_t)c;
}

void log_puts(const char *s)
{
    int locked = log_lock();
    while (*s) {
        log_putc(*s++);
    }
    log_unlock(locked);
}

void log_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log_puts(buf);
}
