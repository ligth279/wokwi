#ifndef FAULT_INJECT_H
#define FAULT_INJECT_H

#include <stdint.h>

/* Number of faults actually injected since boot. Only the injection
 * routines (later step) increment it; accepting or rejecting a FAULT
 * command never does. */
extern volatile uint32_t g_faults_injected;

#endif /* FAULT_INJECT_H */
