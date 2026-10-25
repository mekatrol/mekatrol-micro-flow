/*
 * Minimal RP2040 FreeRTOS firmware.
 *
 * Provides USB CDC console processing and a periodic RGB status heartbeat.
 * No upload transport, flow store, or UART application service is started.
 */
#include "firmware.h"
#include "firmware_internal.h"

#include <microflow/internal/board.h>

#include "rp2040_sdk.h"

/*
 * Firmware state has static storage duration so task control blocks,
 * stacks, and transport buffers remain valid for the device lifetime.
 */
static microflow_firmware_t firmware;

/*
 * Own TinyUSB initialization, CDC servicing, and console command processing.
 *
 * All TinyUSB application calls execute from this task. The periodic delay
 * provides bounded polling latency without continuously consuming CPU.
 */
static void usb_task(void *context) {
    microflow_firmware_t *instance = context;

    /*
     * Initialize TinyUSB after the scheduler has started.
     * The USB controller interrupt is managed by the TinyUSB adapter.
     */
    if (!microflow_rp2040_usb_cdc_initialize(
            &instance->usb,
            instance->usb_receive,
            sizeof(instance->usb_receive),
            instance->usb_transmit,
            sizeof(instance->usb_transmit))) {
        /* Remain in a visible fault state if USB initialization fails. */
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }

    for (;;) {
        /*
         * Poll the USB stack, transfer received bytes to the console ring,
         * and process any complete console commands.
         */
        microflow_firmware_usb_service(instance);

        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

/*
 * Display a periodic heartbeat independently of USB console processing.
 *
 * The heartbeat demonstrates that the FreeRTOS scheduler can run a second
 * application task while USB CDC remains responsive.
 */
static void status_task(void *context) {
    (void)context;

    TickType_t last_wake   = xTaskGetTickCount();
    bool       illuminated = false;

    for (;;) {
        illuminated = !illuminated;

        microflow_board_status_rgb(
            0U,
            illuminated ? 12U : 0U,
            0U);

        vTaskDelayUntil(
            &last_wake,
            pdMS_TO_TICKS(500U));
    }
}

/*
 * Initialize board hardware and portable console state before task creation.
 *
 * Interrupt-driven USB initialization is deliberately deferred until the USB
 * task runs under the FreeRTOS scheduler.
 */
static void initialize(void) {
    microflow_board_initialize();

    microflow_byte_ring_initialize(
        &firmware.console_receive);

    microflow_firmware_console_initialize(
        &firmware);
}

/*
 * Construct the complete static task set and start FreeRTOS.
 *
 * Task creation does not allocate heap memory. The USB task has higher
 * priority than the heartbeat task to keep CDC servicing responsive.
 */
void microflow_firmware_run(void) {
    initialize();

    firmware.usb_task = xTaskCreateStatic(
        usb_task,
        "usb",
        MICROFLOW_USB_TASK_STACK_WORDS,
        &firmware,
        tskIDLE_PRIORITY + 2U,
        firmware.usb_task_stack,
        &firmware.usb_task_tcb);

    firmware.status_task = xTaskCreateStatic(
        status_task,
        "status",
        MICROFLOW_STATUS_TASK_STACK_WORDS,
        &firmware,
        tskIDLE_PRIORITY + 1U,
        firmware.status_task_stack,
        &firmware.status_task_tcb);

    if ((firmware.usb_task == NULL) ||
        (firmware.status_task == NULL)) {
        for (;;) {
            microflow_board_status_rgb(16U, 0U, 0U);
            microflow_board_delay_ms(500U);
        }
    }

    /*
     * Initially constrain application tasks to core 0.
     * FreeRTOS SMP remains enabled, but task ownership is deterministic.
     *
     * Requires configUSE_CORE_AFFINITY enabled in FreeRTOSConfig.h.
     */
#if defined(configUSE_CORE_AFFINITY) && (configUSE_CORE_AFFINITY == 1)
    vTaskCoreAffinitySet(firmware.usb_task, 1U << 0);
    vTaskCoreAffinitySet(firmware.status_task, 1U << 0);
#endif

    vTaskStartScheduler();

    /* The scheduler should never return during normal operation. */
    for (;;) {
        microflow_board_status_rgb(16U, 0U, 0U);
    }
}