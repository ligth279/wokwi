/* Development-only fault dump (compiled with EXTRA_DEFS=-DDEBUG_FAULT).
 * Not part of the baseline or protected experiment builds. */
#ifdef DEBUG_FAULT
#include "log.h"
#include "stm32f1xx.h"

void debug_fault_dump(uint32_t *frame, uint32_t exc_return)
{
    log_printf("[DBGFAULT] vect=%lu exc_ret=0x%08lX msp=0x%08lX psp=0x%08lX\r\n",
               (unsigned long)(SCB->ICSR & 0x1FFu), (unsigned long)exc_return,
               (unsigned long)__get_MSP(), (unsigned long)__get_PSP());
    log_printf("[DBGFAULT] cfsr=0x%08lX hfsr=0x%08lX mmfar=0x%08lX bfar=0x%08lX\r\n",
               (unsigned long)SCB->CFSR, (unsigned long)SCB->HFSR, (unsigned long)SCB->MMFAR,
               (unsigned long)SCB->BFAR);
    log_printf("[DBGFAULT] r0=0x%08lX r1=0x%08lX r2=0x%08lX r3=0x%08lX r12=0x%08lX lr=0x%08lX pc=0x%08lX xpsr=0x%08lX\r\n",
               (unsigned long)frame[0], (unsigned long)frame[1], (unsigned long)frame[2],
               (unsigned long)frame[3], (unsigned long)frame[4], (unsigned long)frame[5],
               (unsigned long)frame[6], (unsigned long)frame[7]);
    for (;;) {
    }
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("movs r0, #4\n" /* ARMv6-M compatible: no IT */
                   "mov r1, lr\n"
                   "tst r0, r1\n"
                   "beq 1f\n"
                   "mrs r0, psp\n"
                   "ldr r2, =debug_fault_dump\n"
                   "bx r2\n"
                   "1: mrs r0, msp\n"
                   "ldr r2, =debug_fault_dump\n"
                   "bx r2\n"
                   ".ltorg\n");
}

#endif
