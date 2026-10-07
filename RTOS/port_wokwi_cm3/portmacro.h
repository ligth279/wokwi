/*
 * FreeRTOS port for Cortex-M3 under the Wokwi STM32 simulator.
 *
 * Derived from the official GCC/ARM_CM3 port (FreeRTOS V11.1.0, MIT
 * licence). Only the parts that depend on the following mis-emulated
 * features are replaced (see docs/SIMULATOR_LIMITATIONS.md):
 *   - PendSV: a single pend re-enters the handler forever
 *   - BASEPRI: not implemented
 *   - PRIMASK: cpsid/cpsie inverted and without masking effect
 *   - SHPR system-handler priorities: not retained
 *
 *   - IT-block state (Thumb-2 conditional execution) is lost across
 *     exceptions, so the project is compiled for the ARMv6-M subset
 *     (-mcpu=cortex-m0), which has no IT instruction; this port's assembly
 *     uses ARMv6-M instructions only
 *
 * Replacement mechanisms:
 *   - context switch in SVC (task yield) and SysTick (tick preemption)
 *   - critical sections gate the SysTick interrupt (SysTick->CTRL.TICKINT);
 *     ticks that elapse while gated are replayed from the DWT cycle counter
 *   - yields requested inside a critical section or an ISR are deferred
 *     (critical exit / next tick), mirroring PendSV semantics
 *
 * RULE: ISRs other than SysTick must not call FreeRTOS APIs, because they
 * cannot be masked by kernel critical sections in this simulator.
 */
#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
extern "C" {
#endif

#define portCHAR       char
#define portFLOAT      float
#define portDOUBLE     double
#define portLONG       long
#define portSHORT      short
#define portSTACK_TYPE uint32_t
#define portBASE_TYPE  long

typedef portSTACK_TYPE StackType_t;
typedef long           BaseType_t;
typedef unsigned long  UBaseType_t;

#if (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_32_BITS)
typedef uint32_t TickType_t;
#define portMAX_DELAY           (TickType_t)0xffffffffUL
#define portTICK_TYPE_IS_ATOMIC 1
#else
#error This port supports 32-bit ticks only.
#endif

#define portSTACK_GROWTH   (-1)
#define portTICK_PERIOD_MS ((TickType_t)1000 / configTICK_RATE_HZ)
#define portBYTE_ALIGNMENT 8
#define portDONT_DISCARD   __attribute__((used))

/* Scheduler utilities */
extern void vPortYield(void);
extern void vPortYieldFromISR(void);
#define portYIELD() vPortYield()
#define portEND_SWITCHING_ISR(xSwitchRequired) \
    do {                                        \
        if ((xSwitchRequired) != pdFALSE) {     \
            vPortYieldFromISR();                \
        }                                       \
    } while (0)
#define portYIELD_FROM_ISR(x) portEND_SWITCHING_ISR(x)

/* Critical section management */
extern void vPortEnterCritical(void);
extern void vPortExitCritical(void);
extern uint32_t ulPortSetInterruptMaskFromISR(void);
extern void vPortClearInterruptMaskFromISR(uint32_t ulMask);
extern void vPortDisableInterrupts(void);
extern void vPortEnableInterrupts(void);

#define portSET_INTERRUPT_MASK_FROM_ISR()      ulPortSetInterruptMaskFromISR()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)   vPortClearInterruptMaskFromISR(x)
#define portDISABLE_INTERRUPTS()               vPortDisableInterrupts()
#define portENABLE_INTERRUPTS()                vPortEnableInterrupts()
#define portENTER_CRITICAL()                   vPortEnterCritical()
#define portEXIT_CRITICAL()                    vPortExitCritical()

#define portTASK_FUNCTION_PROTO(vFunction, pvParameters) void vFunction(void *pvParameters)
#define portTASK_FUNCTION(vFunction, pvParameters)       void vFunction(void *pvParameters)

/* Optimised task selection (CLZ) */
#if configUSE_PORT_OPTIMISED_TASK_SELECTION == 1
__attribute__((always_inline)) static inline uint8_t ucPortCountLeadingZeros(uint32_t ulBitmap)
{
    uint8_t ucReturn;
    __asm volatile("clz %0, %1" : "=r"(ucReturn) : "r"(ulBitmap) : "memory");
    return ucReturn;
}
#if (configMAX_PRIORITIES > 32)
#error configUSE_PORT_OPTIMISED_TASK_SELECTION requires configMAX_PRIORITIES <= 32.
#endif
#define portRECORD_READY_PRIORITY(uxPriority, uxReadyPriorities) (uxReadyPriorities) |= (1UL << (uxPriority))
#define portRESET_READY_PRIORITY(uxPriority, uxReadyPriorities)  (uxReadyPriorities) &= ~(1UL << (uxPriority))
#define portGET_HIGHEST_PRIORITY(uxTopPriority, uxReadyPriorities) \
    uxTopPriority = (31UL - (uint32_t)ucPortCountLeadingZeros((uxReadyPriorities)))
#endif

#define portNOP()
#define portINLINE __inline
#ifndef portFORCE_INLINE
#define portFORCE_INLINE inline __attribute__((always_inline))
#endif

portFORCE_INLINE static BaseType_t xPortIsInsideInterrupt(void)
{
    uint32_t ulCurrentInterrupt;
    __asm volatile("mrs %0, ipsr" : "=r"(ulCurrentInterrupt)::"memory");
    return ulCurrentInterrupt != 0 ? pdTRUE : pdFALSE;
}

#define portMEMORY_BARRIER() __asm volatile("" ::: "memory")

/* Hook called once per elapsed tick (from SysTick or tick replay), also
 * before the scheduler starts. The application uses it for HAL_IncTick(). */
extern void vPortOnTick(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
