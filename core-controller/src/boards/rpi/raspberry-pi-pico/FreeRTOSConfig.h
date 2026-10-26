
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/*
 * Microflow - RP2040 FreeRTOS SMP configuration
 *
 * Target: Raspberry Pi Pico (RP2040)
 * Pico SDK: 2.3.1
 * FreeRTOS Kernel: V11.3.0
 *
 * Two cores, static allocation, no tickless idle.
 */

/*-----------------------------------------------------------
 * Scheduler
 *----------------------------------------------------------*/

#define configNUMBER_OF_CORES         2
#define configNUM_CORES               configNUMBER_OF_CORES
#define configUSE_PREEMPTION          1
#define configUSE_TIME_SLICING        1
#define configRUN_MULTIPLE_PRIORITIES 1
#define configUSE_CORE_AFFINITY       1
#define configUSE_16_BIT_TICKS        0
#define configTICK_RATE_HZ            1000
#define configMAX_PRIORITIES          8
#define configIDLE_SHOULD_YIELD       1
#define configUSE_TICKLESS_IDLE       0

/* RP2040 port: Core 0 owns the tick interrupt. */
#define configTICK_CORE 0

/*-----------------------------------------------------------
 * CPU and interrupts
 *----------------------------------------------------------*/

#define configMAX_SYSCALL_INTERRUPT_PRIORITY 0

/*-----------------------------------------------------------
 * Memory allocation
 *----------------------------------------------------------*/

#define configSUPPORT_STATIC_ALLOCATION  1
#define configSUPPORT_DYNAMIC_ALLOCATION 0

#define configTOTAL_HEAP_SIZE (0)

#define configMINIMAL_STACK_SIZE 256
#define configMAX_TASK_NAME_LEN  16

#define configSTACK_DEPTH_TYPE uint32_t

/*-----------------------------------------------------------
 * Synchronization
 *----------------------------------------------------------*/

#define configUSE_MUTEXES             1
#define configUSE_RECURSIVE_MUTEXES   1
#define configUSE_COUNTING_SEMAPHORES 1
#define configUSE_TASK_NOTIFICATIONS  1

#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1

#define configQUEUE_REGISTRY_SIZE 0

/*-----------------------------------------------------------
 * Software timers
 *----------------------------------------------------------*/

#define configUSE_TIMERS             1
#define configTIMER_TASK_PRIORITY    2
#define configTIMER_QUEUE_LENGTH     8
#define configTIMER_TASK_STACK_DEPTH 256

/*-----------------------------------------------------------
 * Hooks and diagnostics
 *----------------------------------------------------------*/

#define configUSE_IDLE_HOOK                  0
#define configUSE_PASSIVE_IDLE_HOOK          0
#define configUSE_TICK_HOOK                  0
#define configCHECK_FOR_STACK_OVERFLOW       2
#define configUSE_MALLOC_FAILED_HOOK         1
#define configUSE_TRACE_FACILITY             0
#define configGENERATE_RUN_TIME_STATS        0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/*-----------------------------------------------------------
 * Pico SDK interoperability
 *----------------------------------------------------------*/

#define configSUPPORT_PICO_SYNC_INTEROP 1
#define configSUPPORT_PICO_TIME_INTEROP 1

/* Required by RP2040 SMP port */
#define INCLUDE_xEventGroupSetBitsFromISR 1

/* Required by Pico SDK FreeRTOS async context */
#define INCLUDE_xTimerPendFunctionCall   1
#define INCLUDE_xSemaphoreGetMutexHolder 1

/*-----------------------------------------------------------
 * Optional FreeRTOS API functions
 *----------------------------------------------------------*/

#define INCLUDE_vTaskPrioritySet            1
#define INCLUDE_uxTaskPriorityGet           1
#define INCLUDE_vTaskDelete                 1
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_vTaskDelayUntil             1
#define INCLUDE_xTaskGetSchedulerState      1
#define INCLUDE_xTaskGetCurrentTaskHandle   1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle      1

/*-----------------------------------------------------------
 * Assertions
 *----------------------------------------------------------*/

#define configASSERT(x)                             \
    do {                                            \
        if (!(x)) {                                 \
            __asm volatile("cpsid i" ::: "memory"); \
            for (;;) {                              \
                __asm volatile("nop");              \
            }                                       \
        }                                           \
    } while (0)

#endif /* FREERTOS_CONFIG_H */
