
/*
 * Raspberry Pi Pico firmware startup and interrupt entry points.
 *
 * FreeRTOS SMP owns scheduling. All application task storage
 * is statically allocated.
 */
#include "firmware.h"
#include "firmware_internal.h"

#include <microflow/flow.h>
#include <microflow/internal/board.h>

#include "board_gpio_rp2040.h"
#include "rp2040_sdk.h"

/* Single statically allocated firmware instance. */
static microflow_firmware_t firmware;

#define MICROFLOW_CORE0_MASK (1U << 0U)

/*
 * The former supervisor timer alarm is no longer used.
 *
 * Remove its registration from rp2040_sdk.c before removing
 * this compatibility entry point.
 */
void microflow_rp2040_timer_alarm0_event(void) {
}

/*
 * Drain UART RX and TX hardware FIFOs.
 *
 * UART upload parsing happens in the FreeRTOS UART task.
 */
void microflow_rp2040_uart0_event(void) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    bool       received                   = false;

    while (microflow_rp2040_uart0_read_ready()) {
        const uint8_t value = microflow_rp2040_uart0_read();

        (void)microflow_byte_ring_push_from_isr(
            &firmware.uart_receive, value);

        received = true;
    }

    uint8_t value;

    while (microflow_rp2040_uart0_write_ready() &&
           microflow_byte_ring_pop(&firmware.uart_transmit, &value)) {
        microflow_rp2040_uart0_write(value);
    }

    if (microflow_byte_ring_count(&firmware.uart_transmit) == 0U) {
        microflow_rp2040_uart0_set_transmit_interrupt(false);
    }

    if (received && firmware.uart_task != NULL) {
        vTaskNotifyGiveFromISR(
            firmware.uart_task,
            &higher_priority_task_woken);
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/*
 * TinyUSB controller interrupts are handled by its adapter.
 *
 * The USB task polls the adapter at a fixed interval.
 */
void microflow_rp2040_usb_event(void) {
}

/* Initialize hardware, firmware state, and transports. */
static bool initialize(void) {
    microflow_board_initialize();

    microflow_board_gpio_rp2040_initialize(&firmware.gpio);

    microflow_byte_ring_initialize(&firmware.uart_receive);
    microflow_byte_ring_initialize(&firmware.uart_transmit);
    microflow_byte_ring_initialize(&firmware.console_receive);

    microflow_flow_store_initialize(
        &firmware.flow_store,
        firmware.active_flow,
        sizeof(firmware.active_flow),
        microflow_firmware_lock,
        microflow_firmware_unlock,
        microflow_firmware_flow_stopped,
        &firmware);

    microflow_upload_transport_initialize(
        &firmware.upload,
        &firmware.uart_receive,
        &firmware.uart_transmit,
        microflow_flow_validate,
        microflow_flow_store_activate,
        &firmware.flow_store);

    microflow_firmware_console_initialize(&firmware);

    return true;
}

/* USB CDC and console processing. */
static void usb_task(void *context) {
    microflow_firmware_t *instance = context;

    const bool initialized = microflow_rp2040_usb_cdc_initialize(
        &instance->usb,
        instance->usb_receive,
        sizeof(instance->usb_receive),
        instance->usb_transmit,
        sizeof(instance->usb_transmit));

    configASSERT(initialized);

    for (;;) {
        microflow_firmware_usb_service(instance);
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

/* UART upload processing, triggered by RX interrupts. */
/*
 * Own UART upload processing and timeout maintenance in a single FreeRTOS task.
 *
 * The UART interrupt handler transfers received bytes into the fixed receive
 * ring and notifies this task when new input is available. Parsing, image
 * validation, activation, and timeout handling execute in task context rather
 * than interrupt context.
 *
 * This task is the sole task-level owner of the upload transport, preventing
 * concurrent access between upload parsing and expiration processing.
 */
static void uart_task(void *context) {
    /* Recover the statically allocated firmware state passed at task creation. */
    microflow_firmware_t *instance = context;

    /*
     * Configure UART0 after the FreeRTOS scheduler has started.
     *
     * Task entry executes in scheduler context, so enabling UART interrupts
     * here allows the interrupt handler to use FreeRTOS task notifications
     * without encountering the pre-scheduler initialization race.
     */
    microflow_rp2040_uart0_initialize(MICROFLOW_UART_BAUD_RATE);

    /* Remain active for the lifetime of the firmware. */
    for (;;) {
        /*
         * Wait for a UART receive notification or a periodic timeout.
         *
         * pdTRUE clears the accumulated notification count when received.
         * The 10 ms timeout ensures that upload housekeeping continues even
         * when no additional UART bytes arrive.
         *
         * Task notifications coalesce wakeups; the service routine must
         * drain the receive ring rather than assume one byte per notification.
         */
        (void)ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(10U));

        /*
         * Consume queued UART bytes and process upload protocol frames.
         *
         * This may validate and activate a completed flow image, so it must
         * execute outside interrupt context.
         *
         * The service also enables UART transmit interrupts when a protocol
         * response has been queued.
         */
        microflow_firmware_uart_service(instance);

        /*
         * Expire incomplete uploads that have exceeded the configured
         * receive timeout.
         *
         * The board monotonic counter provides the current microsecond tick.
         * The transport implementation performs wrap-safe elapsed-time
         * comparisons and clears stale upload state when necessary.
         *
         * Running expiration in this task keeps it serialized with upload
         * parsing, avoiding concurrent modifications of the staging buffer
         * and parser state by separate FreeRTOS tasks.
         */
        (void)microflow_upload_transport_expire(
            &instance->upload,
            microflow_board_monotonic_ticks(),
            MICROFLOW_UART_RECEIVE_TIMEOUT_US);
    }
}

/* Periodic status, upload timeout, and watchdog maintenance. */
static void status_task(void *context) {
    microflow_firmware_t *instance  = context;
    TickType_t            last_wake = xTaskGetTickCount();

    microflow_rp2040_watchdog_initialize(
        MICROFLOW_WATCHDOG_TIMEOUT_US);

    for (;;) {
        microflow_firmware_status_service(instance);

        microflow_rp2040_watchdog_service();

        vTaskDelayUntil(
            &last_wake,
            pdMS_TO_TICKS(500U));
    }
}
/* Create every application task using caller-owned storage. */
static bool create_tasks(void) {
    firmware.usb_task = xTaskCreateStatic(
        usb_task,
        "usb",
        MICROFLOW_USB_TASK_STACK_WORDS,
        &firmware,
        tskIDLE_PRIORITY + 2U,
        firmware.usb_task_stack,
        &firmware.usb_task_tcb);

    firmware.uart_task = xTaskCreateStatic(
        uart_task,
        "uart",
        MICROFLOW_UART_TASK_STACK_WORDS,
        &firmware,
        tskIDLE_PRIORITY + 2U,
        firmware.uart_task_stack,
        &firmware.uart_task_tcb);

    firmware.status_task = xTaskCreateStatic(
        status_task,
        "status",
        MICROFLOW_STATUS_TASK_STACK_WORDS,
        &firmware,
        tskIDLE_PRIORITY + 1U,
        firmware.status_task_stack,
        &firmware.status_task_tcb);

    if ((firmware.usb_task == NULL) ||
        (firmware.uart_task == NULL) ||
        (firmware.status_task == NULL)) {
        return false;
    }

    /*
     * Initial migration: keep shared application state on core 0.
     * FreeRTOS still manages both cores.
     */
    vTaskCoreAffinitySet(
        firmware.usb_task, MICROFLOW_CORE0_MASK);

    vTaskCoreAffinitySet(
        firmware.uart_task, MICROFLOW_CORE0_MASK);

    vTaskCoreAffinitySet(
        firmware.status_task, MICROFLOW_CORE0_MASK);

    return true;
}

/* Fatal fallback that does not require FreeRTOS services. */
static void fatal(void) {
    for (;;) {
        microflow_board_status_rgb(16U, 0U, 0U);
        microflow_board_delay_ms(500U);
    }
}

/* Initialize firmware, create tasks, and start FreeRTOS SMP. */
void microflow_firmware_run(void) {
    if (!initialize()) {
        fatal();
    }

    if (!create_tasks()) {
        fatal();
    }

    vTaskStartScheduler();

    /* Scheduler startup must not return. */
    configASSERT(0);

    fatal();
}
