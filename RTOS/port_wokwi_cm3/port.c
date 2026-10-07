/*
 * FreeRTOS port for the STM32F103 (Cortex-M3) under the Wokwi simulator.
 * See portmacro.h for why this port exists and how it differs from the
 * official ports it is derived from (FreeRTOS V11.1.0, MIT licence).
 *
 * The code is written in the ARMv6-M instruction subset (the whole project
 * is compiled with -mcpu=cortex-m0, see Makefile) because Wokwi does not
 * preserve Thumb-2 IT-block state across exceptions (smoke test
 * CORE_IT_STATE); the context save/restore therefore follows the official
 * ARM_CM0 port. The resulting task stack layout is the same as ARM_CM3:
 * hardware frame (r0-r3, r12, lr, pc, xPSR) and, below it, r4-r11 in
 * ascending address order; the first TCB member holds the stack top.
 */
#include "FreeRTOS.h"
#include "task.h"

/* SysTick */
#define SYST_CSR (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018UL)
#define SYST_CSR_ENABLE    (1UL << 0)
#define SYST_CSR_TICKINT   (1UL << 1)
#define SYST_CSR_CLKSOURCE (1UL << 2)

/* DWT cycle counter (tick replay time base) */
#define DEMCR      (*(volatile uint32_t *)0xE000EDFCUL)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004UL)

#define portINITIAL_XPSR       (0x01000000UL)
#define portSTART_ADDRESS_MASK ((StackType_t)0xfffffffeUL)

/* Critical nesting is global, as in the official ports. A task is never
 * switched out while it is non-zero (yields are deferred, SysTick gated). */
static volatile UBaseType_t uxCriticalNesting = 0;
static volatile uint32_t ulYieldPending = 0;
static volatile uint32_t ulSchedulerRunning = 0;
static uint32_t ulCyclesPerTick;
static volatile uint32_t ulNextTickCycle;

/* Observability for tests (read by the application, never written). */
volatile uint32_t ulPortReplayedTicks = 0;
volatile uint32_t ulPortContextSwitches = 0;
volatile uint32_t ulPortTickInCritical = 0; /* SysTick seen while gated: must stay 0 */

void SVC_Handler(void) __attribute__((naked));
void SysTick_Handler(void) __attribute__((naked));
void port_switch_context(void) __attribute__((naked));
static void prvPortStartFirstTask(void) __attribute__((naked, noreturn));

__attribute__((weak)) void vPortOnTick(void)
{
}

static void prvTaskExitError(void)
{
    /* A task function returned: not allowed. Stop here (debugger-visible). */
    vPortDisableInterrupts();
    for (;;) {
    }
}

StackType_t *pxPortInitialiseStack(StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters)
{
    pxTopOfStack--;                                     /* as official ports */
    *pxTopOfStack = portINITIAL_XPSR;                   /* xPSR */
    pxTopOfStack--;
    *pxTopOfStack = ((StackType_t)pxCode) & portSTART_ADDRESS_MASK; /* PC */
    pxTopOfStack--;
    *pxTopOfStack = (StackType_t)prvTaskExitError;      /* LR */
    pxTopOfStack -= 5;                                  /* R12, R3, R2, R1 */
    *pxTopOfStack = (StackType_t)pvParameters;          /* R0 */
    pxTopOfStack -= 8;                                  /* R11..R4 */
    return pxTopOfStack;
}

/* ---- tick handling ------------------------------------------------------ */

static inline void prvTickIrqOff(void)
{
    SYST_CSR &= ~SYST_CSR_TICKINT;
}

static inline void prvTickIrqOn(void)
{
    SYST_CSR |= SYST_CSR_TICKINT;
}

/* Process every tick boundary that has passed (DWT-based). With
 * ulTolerance = 0 only boundaries strictly in the past are processed
 * (critical-section exit); the SysTick handler passes half a tick so that
 * small phase offsets between SysTick and DWT never drop a tick. Each
 * boundary is processed exactly once, whichever path reaches it first.
 * Returns the number of ticks processed. */
static uint32_t prvCatchUpTicks(uint32_t ulTolerance)
{
    uint32_t ulNow = DWT_CYCCNT;
    uint32_t n = 0;
    while ((int32_t)(ulNow + ulTolerance - ulNextTickCycle) >= 0) {
        ulNextTickCycle += ulCyclesPerTick;
        n++;
        vPortOnTick();
        if (xTaskIncrementTick() != pdFALSE) {
            ulYieldPending = 1;
        }
    }
    return n;
}

/* Called from SysTick_Handler. Returns non-zero if a context switch is
 * wanted. */
uint32_t port_systick_c(void)
{
    if (!ulSchedulerRunning) {
        vPortOnTick();
        return 0;
    }
    if (uxCriticalNesting != 0) {
        /* TICKINT gating failed to hold the interrupt off. Do not touch
         * kernel state; the ticks are replayed at critical-section exit. */
        ulPortTickInCritical++;
        return 0;
    }
    uint32_t n = prvCatchUpTicks(ulCyclesPerTick / 2u);
    if (n > 1u) {
        ulPortReplayedTicks += n - 1u;
    }
    return ulYieldPending;
}

/* Called from port_switch_context with the outgoing context saved. */
void port_switch_c(void)
{
    prvTickIrqOff();
    ulYieldPending = 0;
    ulPortContextSwitches++;
    vTaskSwitchContext();
    prvTickIrqOn();
}

/* ---- exception handlers (ARMv6-M instructions only) ---------------------- */

/* Save the running task (PSP), select the next one, restore it, return to
 * thread mode on PSP. Entered by branch from SVC/SysTick with LR holding
 * EXC_RETURN 0xFFFFFFFD. */
void port_switch_context(void)
{
    __asm volatile(
        "   mrs r0, psp                 \n"
        "   ldr r3, =pxCurrentTCB       \n"
        "   ldr r2, [r3]                \n"
        "   subs r0, r0, #32            \n" /* room for r4-r11 */
        "   str r0, [r2]                \n" /* save new top of stack */
        "   stmia r0!, {r4-r7}          \n"
        "   mov r4, r8                  \n"
        "   mov r5, r9                  \n"
        "   mov r6, r10                 \n"
        "   mov r7, r11                 \n"
        "   stmia r0!, {r4-r7}          \n"
        "   push {r3, r14}              \n"
        "   bl port_switch_c            \n"
        "   pop {r2, r3}                \n" /* r2 = &pxCurrentTCB, r3 = EXC_RETURN */
        "   ldr r1, [r2]                \n"
        "   ldr r0, [r1]                \n" /* new top of stack */
        "   adds r0, r0, #16            \n"
        "   ldmia r0!, {r4-r7}          \n" /* r8-r11 */
        "   mov r8, r4                  \n"
        "   mov r9, r5                  \n"
        "   mov r10, r6                 \n"
        "   mov r11, r7                 \n"
        "   msr psp, r0                 \n"
        "   subs r0, r0, #32            \n"
        "   ldmia r0!, {r4-r7}          \n" /* r4-r7 */
        "   bx r3                       \n"
        "   .ltorg                      \n");
}

/* SVC from a task (EXC_RETURN 0xFFFFFFFD) = yield. SVC from the MSP thread
 * context (only issued by prvPortStartFirstTask) = start the first task. */
void SVC_Handler(void)
{
    __asm volatile(
        "   ldr r1, =0xFFFFFFFD         \n"
        "   cmp lr, r1                  \n"
        "   bne 1f                      \n"
        "   ldr r0, =port_switch_context\n"
        "   bx r0                       \n"
        "1: ldr r3, =pxCurrentTCB       \n"
        "   ldr r1, [r3]                \n"
        "   ldr r0, [r1]                \n"
        "   adds r0, r0, #16            \n"
        "   ldmia r0!, {r4-r7}          \n"
        "   mov r8, r4                  \n"
        "   mov r9, r5                  \n"
        "   mov r10, r6                 \n"
        "   mov r11, r7                 \n"
        "   msr psp, r0                 \n"
        "   subs r0, r0, #32            \n"
        "   ldmia r0!, {r4-r7}          \n"
        "   ldr r0, =0xFFFFFFFD         \n"
        "   bx r0                       \n"
        "   .ltorg                      \n");
}

/* Tick: process elapsed ticks; switch context only if a task (thread mode,
 * PSP) was interrupted, otherwise leave the switch pending. */
void SysTick_Handler(void)
{
    __asm volatile(
        "   push {r4, lr}               \n"
        "   bl port_systick_c           \n"
        "   pop {r4}                    \n"
        "   pop {r1}                    \n"
        "   mov lr, r1                  \n"
        "   cmp r0, #0                  \n"
        "   beq 1f                      \n"
        "   ldr r2, =0xFFFFFFFD         \n"
        "   cmp r1, r2                  \n"
        "   bne 1f                      \n"
        "   ldr r0, =port_switch_context\n"
        "   bx r0                       \n"
        "1: bx lr                       \n"
        "   .ltorg                      \n");
}

static void prvPortStartFirstTask(void)
{
    __asm volatile(
        "   ldr r0, =_estack            \n" /* reclaim the main stack for ISRs */
        "   msr msp, r0                 \n"
        "   movs r0, #0                 \n" /* PRIMASK=0 via MSR (see prvSvcYield) */
        "   msr primask, r0             \n"
        "   dsb                         \n"
        "   isb                         \n"
        "   svc 0                       \n"
        "2: b 2b                        \n"
        "   .ltorg                      \n");
}

BaseType_t xPortStartScheduler(void)
{
    DEMCR |= (1UL << 24); /* TRCENA */
    DWT_CTRL |= 1UL;      /* CYCCNTENA (never reset CYCCNT after this point) */

    ulCyclesPerTick = configCPU_CLOCK_HZ / configTICK_RATE_HZ;
    SYST_CSR = 0;
    SYST_RVR = ulCyclesPerTick - 1UL;
    SYST_CVR = 0;
    uxCriticalNesting = 0;
    ulYieldPending = 0;
    ulNextTickCycle = DWT_CYCCNT + ulCyclesPerTick;
    ulSchedulerRunning = 1;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;

    prvPortStartFirstTask(); /* does not return */
}

void vPortEndScheduler(void)
{
    /* Not implemented in ports that have nothing to return to. */
}

/* ---- yield / critical sections ------------------------------------------ */

/* Issue the yield SVC. PRIMASK is cleared first with MSR because, in Wokwi,
 * `cpsie i` (CMSIS __enable_irq(), used e.g. by the HAL I2C driver) SETS
 * PRIMASK, and an SVC raised while PRIMASK=1 stays pending instead of being
 * taken (smoke test CORE_SVC_PRIMASK). On silicon PRIMASK is already 0 here
 * (an SVC with PRIMASK=1 would escalate to HardFault), so this is a no-op. */
static inline void prvSvcYield(void)
{
    __asm volatile("   movs r0, #0     \n"
                   "   msr primask, r0 \n"
                   "   isb             \n"
                   "   svc 0           \n" ::: "r0", "memory");
}

void vPortYield(void)
{
    if (xPortIsInsideInterrupt() || uxCriticalNesting != 0 || !ulSchedulerRunning) {
        ulYieldPending = 1; /* taken at critical exit or at the next tick */
        return;
    }
    prvSvcYield();
}

void vPortYieldFromISR(void)
{
    ulYieldPending = 1;
}

void vPortDisableInterrupts(void)
{
    prvTickIrqOff();
}

void vPortEnableInterrupts(void)
{
    prvTickIrqOn();
}

void vPortEnterCritical(void)
{
    prvTickIrqOff();
    uxCriticalNesting++;
}

void vPortExitCritical(void)
{
    if (uxCriticalNesting == 0) {
        return;
    }
    if (--uxCriticalNesting == 0) {
        if (ulSchedulerRunning) {
            ulPortReplayedTicks += prvCatchUpTicks(0);
        }
        prvTickIrqOn();
        if (ulYieldPending && ulSchedulerRunning && !xPortIsInsideInterrupt()) {
            prvSvcYield();
        }
    }
}

uint32_t ulPortSetInterruptMaskFromISR(void)
{
    uint32_t was_on = (SYST_CSR & SYST_CSR_TICKINT) != 0;
    prvTickIrqOff();
    return was_on;
}

void vPortClearInterruptMaskFromISR(uint32_t ulMask)
{
    if (ulMask) {
        prvTickIrqOn();
    }
}
