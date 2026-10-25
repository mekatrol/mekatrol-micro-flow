/*
 * Own the single firmware instance and its IRQ entry points together so every
 * interrupt signals the same queues and supervisor initialized at startup.
 */
#include "firmware.h"
#include "firmware_internal.h"

#include <microflow/flow.h>
#include <microflow/internal/board.h>

#include "board_gpio_rp2040.h"
#include "rp2040_sdk.h"

/* Static ownership makes the complete firmware RAM budget visible to the linker. */
static microflow_firmware_t firmware;

/* Defer timer-alarm work from the SoC ISR into the cooperative supervisor. */
void microflow_rp2040_timer_alarm0_event(void) {
    /* The ISR only coalesces work; the registered service runs in main context. */
    microflow_supervisor_signal_from_isr(
        &firmware.supervisor, MICROFLOW_SUPERVISOR_EVENT_TIMER);
}

/* Drain/fill UART hardware FIFOs and report which deferred work became ready. */
void microflow_rp2040_uart0_event(void) {
    /* Finite hardware FIFOs and fixed software rings bound the ISR's work. */
    uint32_t events = 0U;
    while (microflow_rp2040_uart0_read_ready()) {
        (void)microflow_supervisor_byte_ring_push_from_isr(
            &firmware.supervisor, &firmware.uart_receive,
            microflow_rp2040_uart0_read());
        events |= MICROFLOW_SUPERVISOR_EVENT_UART0_RX;
    }
    uint8_t value;
    while (microflow_rp2040_uart0_write_ready() &&
           microflow_byte_ring_pop(&firmware.uart_transmit, &value)) {
        microflow_rp2040_uart0_write(value);
        events |= MICROFLOW_SUPERVISOR_EVENT_UART0_TX;
    }
    if (microflow_byte_ring_count(&firmware.uart_transmit) == 0U) {
        /* Avoid repeated empty interrupts until foreground code queues a reply. */
        microflow_rp2040_uart0_set_transmit_interrupt(false);
    }
    if (events != 0U) {
        microflow_supervisor_signal_from_isr(&firmware.supervisor, events);
    }
}

/* Coalesce a USB controller interrupt into one supervisor event bit. */
void microflow_rp2040_usb_event(void) {
    /* TinyUSB retains controller detail; foreground polling consumes it later. */
    microflow_supervisor_signal_from_isr(
        &firmware.supervisor, MICROFLOW_SUPERVISOR_EVENT_USB);
}

/* Compose all board adapters and services before peripheral interrupts start. */
static bool initialize(void) {
    /* Build dependencies from hardware outward so callbacks always see valid state. */
    microflow_board_initialize();
    microflow_board_gpio_rp2040_initialize(&firmware.gpio);
    microflow_byte_ring_initialize(&firmware.uart_receive);
    microflow_byte_ring_initialize(&firmware.uart_transmit);
    microflow_byte_ring_initialize(&firmware.console_receive);
    microflow_flow_store_initialize(
        &firmware.flow_store, firmware.active_flow,
        sizeof(firmware.active_flow), microflow_firmware_lock,
        microflow_firmware_unlock, microflow_firmware_flow_stopped, &firmware);
    microflow_upload_transport_initialize(
        &firmware.upload, &firmware.uart_receive, &firmware.uart_transmit,
        microflow_flow_validate, microflow_flow_store_activate,
        &firmware.flow_store);
    microflow_firmware_console_initialize(&firmware);
    microflow_rp2040_watchdog_initialize(MICROFLOW_WATCHDOG_TIMEOUT_US);
    const microflow_supervisor_platform_t platform =
        microflow_firmware_platform(&firmware);
    /* Every service is mandatory; a partial scheduler is unsafe to operate. */
    if (!microflow_supervisor_initialize(
            &firmware.supervisor, &platform,
            MICROFLOW_HEARTBEAT_PERIOD_US) ||
        !microflow_supervisor_add_service(
            &firmware.supervisor, 10U, MICROFLOW_SUPERVISOR_EVENT_TIMER,
            MICROFLOW_STATUS_SERVICE_BUDGET_US,
            microflow_firmware_status_service, &firmware) ||
        !microflow_supervisor_add_service(
            &firmware.supervisor, 2U,
            MICROFLOW_SUPERVISOR_EVENT_UART0_RX |
                MICROFLOW_SUPERVISOR_EVENT_UART0_TX,
            MICROFLOW_UART_SERVICE_BUDGET_US,
            microflow_firmware_uart_service, &firmware) ||
        !microflow_supervisor_add_service(
            &firmware.supervisor, 1U, MICROFLOW_SUPERVISOR_EVENT_USB,
            MICROFLOW_USB_SERVICE_BUDGET_US, microflow_firmware_usb_service,
            &firmware)) {
        return false;
    }
    /* IRQ-producing peripherals start only after queues and services exist. */
    microflow_rp2040_uart0_initialize(MICROFLOW_UART_BAUD_RATE);
    if (!microflow_rp2040_usb_cdc_initialize(
            &firmware.usb, firmware.usb_receive, sizeof(firmware.usb_receive),
            firmware.usb_transmit, sizeof(firmware.usb_transmit))) {
        microflow_supervisor_report_fault(
            &firmware.supervisor,
            MICROFLOW_SUPERVISOR_FAULT_CONFIGURATION);
    }
    return true;
}

/* Initialize the application, fail visibly if necessary, then run forever. */
void microflow_firmware_run(void) {
    if (!initialize()) {
        for (;;) {
            /* A red half-second blink is the allocation-free fatal fallback. */
            microflow_board_status_rgb(16U, 0U, 0U);
            microflow_board_delay_ms(500U);
        }
    }
    /* The cooperative supervisor owns control for the rest of execution. */
    microflow_supervisor_run(&firmware.supervisor);
}
