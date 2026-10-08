/* CPU load measurement for the overhead comparison (Step 7). Built only with CPU_STATS=1.
 * Counts the DWT cycles during which the idle task runs; everything else (application tasks, ISRs,
 * scheduler, detection/recovery code) is "busy". The same counters are used in the baseline and the
 * protected build, so their own cost cancels out of the comparison. */
#include "FreeRTOS.h"
#include "task.h"
#include "log.h"
#include "stm32f1xx.h"

static volatile uint32_t in_cyc, idle_cyc, start_cyc;
static volatile uint8_t started;

void cpu_stats_in(void)
{
    in_cyc = DWT->CYCCNT;
    if (!started) {
        started = 1;
        start_cyc = in_cyc;
    }
}

void cpu_stats_out(void)
{
    uint32_t d = DWT->CYCCNT - in_cyc;
    if (xTaskGetCurrentTaskHandle() == xTaskGetIdleTaskHandle()) {
        idle_cyc += d;
    }
}

void cpu_stats_print(void)
{
    uint32_t total = DWT->CYCCNT - start_cyc;
    uint32_t idle = idle_cyc;
    uint32_t busy = total - idle;
    LOG("CPUSTAT", "total_cycles=%lu idle_cycles=%lu busy_cycles=%lu busy_permille=%lu", (unsigned long)total, (unsigned long)idle,
        (unsigned long)busy, (unsigned long)((uint64_t)busy * 1000u / total));
}
