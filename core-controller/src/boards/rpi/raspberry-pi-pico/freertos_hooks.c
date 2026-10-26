
#include "FreeRTOS.h"
#include "task.h"

/*
 * Called when FreeRTOS detects a task stack overflow.
 *
 * Enabled by:
 *   configCHECK_FOR_STACK_OVERFLOW = 2
 *
 * This is a fatal error. Do not attempt to continue scheduling.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    (void)pcTaskName;

    __asm volatile("cpsid i" ::: "memory");

    for (;;) {
        __asm volatile("nop");
    }
}

/*
 * Called when a FreeRTOS dynamic memory allocation fails.
 *
 * Enabled by:
 *   configUSE_MALLOC_FAILED_HOOK = 1
 *
 * With configSUPPORT_DYNAMIC_ALLOCATION = 0, FreeRTOS
 * dynamic allocation APIs are disabled. This hook is
 * retained for configuration compatibility.
 */
void vApplicationMallocFailedHook(void) {
    __asm volatile("cpsid i" ::: "memory");

    for (;;) {
        __asm volatile("nop");
    }
}
