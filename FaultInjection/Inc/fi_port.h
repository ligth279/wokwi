#ifndef FI_PORT_H
#define FI_PORT_H

#include <stdint.h>

/* Hardware interface of the fault-injection framework. fault_fw.c uses only
 * these functions, so the framework logic can be compiled and tested on the
 * host (Tests/unit/test_fault_fw.c provides a fake implementation).
 * Target implementation: FaultInjection/Src/fi_port_stm32.c. */

void fi_port_init(void);       /* before the scheduler starts         */
/* Serialises event printing between tasks. Returns non-zero if the lock was
 * taken (not from ISRs / before the scheduler); pass that to unlock. */
int  fi_port_log_lock(void);
void fi_port_log_unlock(int locked);

uint32_t fi_port_cycles(void); /* DWT cycle counter                   */
uint32_t fi_port_ms(void);     /* millisecond tick (HAL_GetTick)      */

/* One-shot timer for the TIMER mechanism: after delay_ms, call
 * fi_timer_expired() from interrupt context, once. */
void fi_port_timer_start(uint32_t delay_ms);
void fi_port_timer_stop(void);

/* Implemented by fault_fw.c, called by the port's timer interrupt. */
void fi_timer_expired(void);

#endif /* FI_PORT_H */
