/* Host stub of Logging/Inc/log.h for unit tests: LOG() lines are captured
 * (and echoed with -v) so tests can assert on the exact log output. */
#ifndef LOG_H
#define LOG_H
void host_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(tag, fmt, ...) host_log("[" tag "] " fmt, ##__VA_ARGS__)
#endif
