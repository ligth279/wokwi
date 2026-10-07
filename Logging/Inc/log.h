#ifndef LOG_H
#define LOG_H

#include <stdint.h>

/* All output goes through a polled USART1 writer that touches only the
 * USART registers, so it is safe from thread mode, ISRs and fault handlers.
 * Line format: "[TAG] key=value key=value\r\n" (see CLAUDE.md section 29). */

#ifdef USE_FREERTOS
void log_rtos_init(void); /* create the line mutex; call before the scheduler starts */
#endif

void log_putc(char c);
void log_puts(const char *s);
void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define LOG(tag, fmt, ...) log_printf("[" tag "] " fmt "\r\n", ##__VA_ARGS__)

#endif /* LOG_H */
