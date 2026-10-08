#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* FreeRTOS V11.1.0 configuration for STM32F103C8 @ 72 MHz (Cortex-M3),
 * using the project's Wokwi-compatible port (RTOS/port_wokwi_cm3).
 *
 * Kernel-level protections are deliberately OFF here (stack-overflow
 * checking, configASSERT, malloc-failed hook). The baseline firmware must
 * show the raw impact of faults; detection mechanisms are added explicitly,
 * and only in the protected build (later step). */

#include <stdint.h>
extern uint32_t SystemCoreClock;

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0 /* no CLZ in ARMv6-M build */
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                ((uint16_t)96)
#define configMAX_TASK_NAME_LEN                 12
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configUSE_MINI_LIST_ITEM                1
#define configSTACK_DEPTH_TYPE                  uint16_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t

/* Memory */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t)(8 * 1024))
#define configAPPLICATION_ALLOCATED_HEAP        0

/* Hooks: all off in the baseline. */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/* Run-time stats / trace */
#define configGENERATE_RUN_TIME_STATS           0
#ifdef PROTECTED_RTOS
#define configUSE_TRACE_FACILITY                1 /* vTaskGetInfo: stack base of each task */
#else
#define configUSE_TRACE_FACILITY                0
#endif
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* Software timers: not used. */
#define configUSE_TIMERS                        0
#define configUSE_CO_ROUTINES                   0

/* Cortex-M3 interrupt priorities (4 priority bits on STM32F1). */
#define configPRIO_BITS                              4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* API inclusion */
#define INCLUDE_vTaskPrioritySet            0
#define INCLUDE_uxTaskPriorityGet           0
#define INCLUDE_vTaskDelete                 1
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_xTaskDelayUntil             1
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_xTaskGetSchedulerState      1
#define INCLUDE_xTaskGetCurrentTaskHandle   1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_eTaskGetState               1
#define INCLUDE_xTaskGetHandle              1

/* Protected build: the detection layer seals the saved context of every task
 * when it is switched out (FaultDetection/Src/det_monitor.c). */
#ifdef PROTECTED_RTOS
extern void det_trace_out(void);
#define traceTASK_SWITCHED_OUT() det_trace_out()
#endif

/* SVC_Handler and SysTick_Handler are defined by the Wokwi-compatible port
 * (RTOS/port_wokwi_cm3). PendSV is not used by that port. */

#endif /* FREERTOS_CONFIG_H */
