/*
 * Adapt the portable line console to the minimal RP2040 FreeRTOS firmware.
 *
 * The firmware currently provides USB CDC communication, FreeRTOS task
 * diagnostics, an RGB heartbeat, and reboot support.
 *
 * Flow storage, upload transport, and application GPIO commands are not
 * enabled in this initial FreeRTOS integration.
 */
#include "firmware_internal.h"

#include "rp2040_sdk.h"

#include <stdint.h>
#include <stdio.h>

/*
 * Send an unsigned integer through the allocation-free USB formatter.
 *
 * This avoids pulling printf support into the firmware.
 */
static void number(microflow_firmware_t *firmware, uint32_t value) {
    microflow_firmware_usb_write_u32(firmware, value);
}

/*
 * Forward solicited console responses to the USB CDC transmit buffer.
 *
 * The console is serviced exclusively by the USB task, so these writes
 * execute in the same task context as TinyUSB processing.
 */
static void response(void *context, const uint8_t *data, size_t length) {
    microflow_firmware_usb_write(context, data, length);
}

/*
 * Prefix asynchronous diagnostics so host software can distinguish them
 * from replies to commands.
 */
static void log_message(void *context, const uint8_t *data, size_t length) {
    microflow_firmware_t *firmware = context;

    microflow_firmware_usb_write_text(firmware, "LOG ");
    microflow_firmware_usb_write(firmware, data, length);
}

/* Identify the minimal firmware and report the RP2040 system clock.
 *
 * Flow-image support is intentionally not advertised until the upload
 * transport and flow store are integrated.
 */
static void info(void *context) {
    char message[80];

    /* Preserve the existing firmware identification response. */
    microflow_firmware_usb_write_text(
        context,
        "OK Micro Flow Raspberry Pi Pico FreeRTOS CDC v1\r\n");

    /* Read the actual system clock frequency configured by the Pico SDK. */
    const uint32_t clock_hz = microflow_rp2040_system_clock_hz();

    const int length = snprintf(
        message,
        sizeof(message),
        "System clock: %lu Hz\r\n",
        (unsigned long)clock_hz);

    /* Send the formatted response only if it fits in the buffer. */
    if (length > 0 && (size_t)length < sizeof(message)) {
        microflow_firmware_usb_write_text(context, message);
    }
}

/*
 * Report the current firmware operating mode.
 *
 * This milestone does not contain a flow engine or upload service.
 */
static void status(void *context) {
    microflow_firmware_usb_write_text(
        context,
        "OK usb=enabled flow=disabled\r\n");
}

/*
 * Report FreeRTOS task stack high-water marks.
 *
 * The values represent the minimum remaining stack space observed since
 * task creation, measured in StackType_t words rather than bytes.
 *
 * Both task handles are valid by the time the scheduler starts.
 */
static void tasks(void *context) {
    microflow_firmware_t *firmware = context;

    microflow_firmware_usb_write_text(firmware, "OK tasks=2");

    microflow_firmware_usb_write_text(
        firmware, " usb-stack-free=");
    number(
        firmware,
        (uint32_t)uxTaskGetStackHighWaterMark(firmware->usb_task));

    microflow_firmware_usb_write_text(
        firmware, " status-stack-free=");
    number(
        firmware,
        (uint32_t)uxTaskGetStackHighWaterMark(firmware->status_task));

    microflow_firmware_usb_write_text(firmware, "\r\n");
}

/*
 * Report bounded console and USB transport diagnostics.
 *
 * These counters allow receive/transmit capacity and command parsing
 * behavior to be observed during hardware testing.
 */
static void queues(void *context) {
    microflow_firmware_t *firmware = context;

    microflow_firmware_usb_write_text(
        firmware, "OK console-rx-high=");
    number(firmware, firmware->console_receive.high_water);

    microflow_firmware_usb_write_text(
        firmware, " console-rx-dropped=");
    number(firmware, firmware->console_receive.rejected);

    microflow_firmware_usb_write_text(
        firmware, " usb-rx-high=");
    number(firmware, firmware->usb.receive_high_water);

    microflow_firmware_usb_write_text(
        firmware, " usb-tx-high=");
    number(firmware, firmware->usb.transmit_high_water);

    microflow_firmware_usb_write_text(
        firmware, " usb-rx-dropped=");
    number(firmware, firmware->usb.receive_dropped);

    microflow_firmware_usb_write_text(
        firmware, " usb-tx-dropped=");
    number(firmware, firmware->usb.transmit_dropped);

    microflow_firmware_usb_write_text(
        firmware, " commands=");
    number(firmware, firmware->console.commands);

    microflow_firmware_usb_write_text(
        firmware, " invalid=");
    number(firmware, firmware->console.invalid_commands);

    microflow_firmware_usb_write_text(
        firmware, " overlong=");
    number(firmware, firmware->console.overlong_commands);

    microflow_firmware_usb_write_text(firmware, "\r\n");
}

/*
 * Indicate that active flow storage is not available in this build.
 */
static void flow_show(void *context) {
    microflow_firmware_usb_write_text(
        context, "ERR flow-disabled\r\n");
}

/*
 * Reject validation requests until active flow storage is implemented.
 */
static bool flow_validate(void *context) {
    (void)context;
    return false;
}

/*
 * Reject flow start requests until the execution engine is implemented.
 */
static bool flow_start(void *context) {
    (void)context;
    return false;
}

/*
 * Reject flow stop requests because this firmware has no flow lifecycle.
 */
static bool flow_stop(void *context) {
    (void)context;
    return false;
}

/*
 * Preserve the console callback contract without maintaining staging
 * storage that is not present in this firmware.
 */
static void flow_erase(void *context) {
    microflow_firmware_usb_write_text(
        context, "ERR flow-disabled\r\n");
}

/*
 * Indicate that application GPIO resources are not exposed by this build.
 *
 * The status LED remains controlled internally by the heartbeat task.
 */
static void gpio_list(void *context) {
    microflow_firmware_usb_write_text(
        context, "OK\r\n");
}

/*
 * Reject GPIO reads while application GPIO support is disabled.
 */
static bool gpio_read(void *context, uint32_t resource, bool *value) {
    (void)context;
    (void)resource;
    (void)value;

    return false;
}

/*
 * Reject GPIO writes while application GPIO support is disabled.
 */
static bool gpio_write(void *context, uint32_t resource, bool value) {
    (void)context;
    (void)resource;
    (void)value;

    return false;
}

/*
 * Reboot the RP2040 using the existing platform reset primitive.
 *
 * This callback does not return during normal operation.
 */
static void reboot(void *context) {
    (void)context;
    microflow_rp2040_reboot();
}

/*
 * Bind portable console commands to the minimal firmware implementation.
 *
 * The operation table is copied by microflow_console_initialize(), so
 * this stack-local configuration does not need persistent storage.
 *
 * All callbacks execute in the USB task when console input is processed.
 */
void microflow_firmware_console_initialize(
    microflow_firmware_t *firmware) {
    const microflow_console_operations_t operations = {
        .context            = firmware,
        .write_response     = response,
        .write_log          = log_message,
        .info               = info,
        .status             = status,
        .tasks              = tasks,
        .queues             = queues,
        .flow_show          = flow_show,
        .flow_validate      = flow_validate,
        .flow_start         = flow_start,
        .flow_stop          = flow_stop,
        .flow_erase_staging = flow_erase,
        .gpio_list          = gpio_list,
        .gpio_read          = gpio_read,
        .gpio_write         = gpio_write,
        .reboot             = reboot
    };

    microflow_console_initialize(
        &firmware->console,
        &firmware->console_receive,
        &operations);
}