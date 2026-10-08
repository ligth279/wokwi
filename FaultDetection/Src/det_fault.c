/* Fault handlers: MemManage, BusFault, UsageFault and HardFault capture the
 * fault status registers, report them with the active experiment ID and stop
 * (detection only; recovery is a later step).
 *
 * Wokwi does NOT deliver these exceptions (Tests/exc probe, results/raw/step5/
 * probes): UDF, an unmapped read, an unaligned access with UNALIGN_TRP and a
 * divide by zero with DIV_0_TRP all run on without any exception, the
 * SHCSR enable bits read back as 0, and a CPU fault that cannot execute ends
 * the simulation. The capture/report path is therefore exercised through
 * det_fault_selftest(), a synthetic invocation with made-up register values
 * (logged as synthetic=1), not through a real CPU fault. */
#include "detect.h"

#if PROTECTED

#include "board.h"
#include "log.h"

#include <stdio.h>

volatile uint32_t det_fault_selftest_done;
volatile uint8_t  det_selftest_request;

void det_fault_enable(void)
{
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
    uint32_t s = SCB->SHCSR;
    LOG("DET", "fault_handlers requested=MemManage,BusFault,UsageFault shcsr=0x%08lX memfault=%d busfault=%d usagefault=%d",
        (unsigned long)s, !!(s & SCB_SHCSR_MEMFAULTENA_Msk), !!(s & SCB_SHCSR_BUSFAULTENA_Msk),
        !!(s & SCB_SHCSR_USGFAULTENA_Msk));
}

static void capture(const char *name, uint32_t cfsr, uint32_t hfsr, uint32_t mmfar, uint32_t bfar, const uint32_t *frame,
                    int synthetic)
{
    char c[96], h[48], extra[64];
    int n = 0;
    extra[0] = '\0';
    if (cfsr & DET_CFSR_MMARVALID) {
        n += snprintf(extra + n, sizeof extra - (unsigned)n, " mmfar=0x%08lX", (unsigned long)mmfar);
    }
    if (cfsr & DET_CFSR_BFARVALID) {
        n += snprintf(extra + n, sizeof extra - (unsigned)n, " bfar=0x%08lX", (unsigned long)bfar);
    }
    det_report(DET_M_FAULT_HANDLER,
               "exception=%s cfsr=0x%08lX(%s) hfsr=0x%08lX(%s)%s stacked_pc=0x%08lX stacked_lr=0x%08lX synthetic=%d", name,
               (unsigned long)cfsr, det_decode_cfsr(cfsr, c, sizeof c), (unsigned long)hfsr,
               det_decode_hfsr(hfsr, h, sizeof h), extra, (unsigned long)frame[6], (unsigned long)frame[5], synthetic);
}

void det_fault_c(const uint32_t *frame, const char *name)
{
    capture(name, SCB->CFSR, SCB->HFSR, SCB->MMFAR, SCB->BFAR, frame, 0);
    for (;;) {
    }
}

void det_fault_selftest(void)
{
    static const uint32_t fake_frame[8] = {0, 0, 0, 0, 0, 0x08001235u, 0x08001234u, 0x01000000u}; /* r0-r3,r12,lr,pc,xpsr */
    capture("HardFault", 0x00020000u /* UsageFault: INVSTATE */, 0x40000000u /* FORCED */, 0, 0, fake_frame, 1);
    det_fault_selftest_done = 1;
}

/* referenced from the assembly trampolines below */
static const char n_hard[] __attribute__((used)) = "HardFault", n_mem[] __attribute__((used)) = "MemManage",
                  n_bus[] __attribute__((used)) = "BusFault", n_use[] __attribute__((used)) = "UsageFault";

/* ARMv6-M instruction subset only (no IT): pick the stack the fault came from,
 * pass its frame and the exception name to det_fault_c(). */
#define TRAMPOLINE(fn, name)                                      \
    __attribute__((naked)) void fn(void)                          \
    {                                                             \
        __asm volatile("movs r0, #4\n"                            \
                       "mov r1, lr\n"                             \
                       "tst r0, r1\n"                             \
                       "beq 1f\n"                                 \
                       "mrs r0, psp\n"                            \
                       "b 2f\n"                                   \
                       "1: mrs r0, msp\n"                         \
                       "2: ldr r1, =" #name "\n"                  \
                       "ldr r2, =det_fault_c\n"                   \
                       "bx r2\n"                                  \
                       ".ltorg\n");                               \
    }
TRAMPOLINE(HardFault_Handler, n_hard)
TRAMPOLINE(MemManage_Handler, n_mem)
TRAMPOLINE(BusFault_Handler, n_bus)
TRAMPOLINE(UsageFault_Handler, n_use)

#endif /* PROTECTED */
