/*
 * Adapt portable console operations to Pico firmware state so the parser stays
 * independent of board-specific storage, GPIO, and USB transport details.
 */
#include "firmware_internal.h"

#include <microflow/version.h>

#include "rp2040_sdk.h"

/*
 * Send counters through the allocation-free USB formatter so diagnostics do
 * not pull printf support into the firmware or duplicate number conversion.
 */
static void number(microflow_firmware_t *f, uint32_t value) {
    microflow_firmware_usb_write_u32(f, value);
}

/*
 * Route parser replies through the firmware USB queue so portable parsing code
 * does not depend on TinyUSB or the Pico's transport state.
 */
static void response(void *context, const uint8_t *data, size_t length) {
    microflow_firmware_usb_write(context, data, length);
}

/*
 * Prefix asynchronous diagnostics with "LOG " so a host can distinguish them
 * from solicited command replies on the shared CDC byte stream.
 */
static void log_message(void *context, const uint8_t *data, size_t length) {
    microflow_firmware_t *f = context;
    microflow_firmware_usb_write_text(f, "LOG ");
    microflow_firmware_usb_write(f, data, length);
}

/*
 * Report the board, console protocol, and flow version so operators and host
 * tools can reject incompatible command or image formats before using them.
 */
static void info(void *context) {
    microflow_firmware_usb_write_text(
        context, "OK Micro Flow Raspberry Pi Pico CDC v1 flow-version=");
    microflow_firmware_usb_write_text(context, microflow_version_string());
    microflow_firmware_usb_write_text(context, "\r\n");
}

/*
 * Report lifecycle, image size, version, and faults so a host can assess the
 * running system without reading or exposing internal firmware state.
 */
static void status(void *context) {
    microflow_firmware_t *f = context;

    microflow_firmware_usb_write_text(f, "OK flow=");
    microflow_firmware_usb_write_text(
        f, f->flow_store.running ? "running" : "stopped");

    microflow_firmware_usb_write_text(f, " active-bytes=");
    number(f, (uint32_t)f->flow_store.length);

    microflow_firmware_usb_write_text(f, " flow-version=");
    microflow_firmware_usb_write_text(f, microflow_version_string());

    microflow_firmware_usb_write_text(f, "\r\n");
}
/*
 * Report scheduler progress and service runtimes so field diagnostics can find
 * stalled work and services that exceed their cooperative execution budgets.
 */
static void tasks(void *context) {
    microflow_firmware_t *f = context;

    microflow_firmware_usb_write_text(f, "OK tasks=3");

    microflow_firmware_usb_write_text(f, " usb-stack-free=");
    number(f, (uint32_t)uxTaskGetStackHighWaterMark(f->usb_task));

    microflow_firmware_usb_write_text(f, " uart-stack-free=");
    number(f, (uint32_t)uxTaskGetStackHighWaterMark(f->uart_task));

    microflow_firmware_usb_write_text(f, " status-stack-free=");
    number(f, (uint32_t)uxTaskGetStackHighWaterMark(f->status_task));

    microflow_firmware_usb_write_text(f, "\r\n");
}

/*
 * Report queue high-water marks and loss counters so fixed buffer capacities
 * can be tuned from observed traffic instead of guessed peak demand.
 */
static void queues(void *context) {
    microflow_firmware_t *f = context;
    /* Cover both directions and transports because each queue is sized independently. */
    microflow_firmware_usb_write_text(f, "OK uart-rx-high=");
    number(f, f->uart_receive.high_water);
    microflow_firmware_usb_write_text(f, " uart-tx-high=");
    number(f, f->uart_transmit.high_water);
    microflow_firmware_usb_write_text(f, " uart-rx-dropped=");
    number(f, f->uart_receive.rejected);
    microflow_firmware_usb_write_text(f, " uart-tx-dropped=");
    number(f, f->uart_transmit.rejected);
    microflow_firmware_usb_write_text(f, " usb-rx-high=");
    number(f, f->usb.receive_high_water);
    microflow_firmware_usb_write_text(f, " usb-tx-high=");
    number(f, f->usb.transmit_high_water);
    microflow_firmware_usb_write_text(f, " usb-rx-dropped=");
    number(f, f->usb.receive_dropped);
    microflow_firmware_usb_write_text(f, " usb-tx-dropped=");
    number(f, f->usb.transmit_dropped);
    microflow_firmware_usb_write_text(f, "\r\n");
}

/*
 * Report active and staged lengths so upload progress can be diagnosed without
 * disclosing or transferring the executable flow image itself.
 */
static void flow_show(void *context) {
    microflow_firmware_t *f = context;
    microflow_firmware_usb_write_text(f, "OK active-bytes=");
    number(f, (uint32_t)f->flow_store.length);
    microflow_firmware_usb_write_text(f, " staging-bytes=");
    number(f, f->upload.received_length);
    microflow_firmware_usb_write_text(f, "\r\n");
}

/*
 * Revalidate the active image on request so corruption can be detected without
 * changing whether the image is currently running.
 */
static bool flow_validate(void *context) {
    microflow_firmware_t *f = context;
    return microflow_flow_store_validate(&f->flow_store);
}

/*
 * Delegate start to the flow store so the console cannot mark an invalid or
 * missing active image as runnable.
 */
static bool flow_start(void *context) {
    microflow_firmware_t *f = context;
    return microflow_flow_store_start(&f->flow_store);
}

/*
 * Stop through the flow store so its lifecycle callback also returns physical
 * outputs to their safe input state.
 */
static bool flow_stop(void *context) {
    microflow_firmware_t *f = context;
    microflow_flow_store_stop(&f->flow_store);
    return true;
}

/*
 * Erase only upload staging so an operator can abandon partial transfer data
 * without disrupting the independently stored active image.
 */
static void flow_erase(void *context) {
    microflow_firmware_t *f = context;
    microflow_upload_transport_erase_staging(&f->upload);
}

/*
 * Advertise stable logical GPIO IDs so host commands remain portable across
 * board revisions with different physical pin mappings.
 */
static void gpio_list(void *context) {
    microflow_firmware_usb_write_text(context,
                                      "OK 0 1 2 3 status-led\r\n");
}

/*
 * Read through the logical GPIO facade so that module, rather than the console,
 * validates resource IDs and translates them to physical pins.
 */
static bool gpio_read(void *context, uint32_t resource, bool *value) {
    microflow_firmware_t *f = context;
    return microflow_board_gpio_read(&f->gpio, resource, value);
}

/*
 * Configure and drive through the logical GPIO facade so it validates the
 * resource and latches it low before enabling output drive.
 */
static bool gpio_write(void *context, uint32_t resource, bool value) {
    microflow_firmware_t *f = context;
    /* Configure first because the GPIO facade rejects writes to input resources. */
    return microflow_board_gpio_configure(
               &f->gpio, resource, MICROFLOW_BOARD_GPIO_OUTPUT) &&
           microflow_board_gpio_write(&f->gpio, resource, value);
}

/*
 * Map the portable reboot operation to the RP2040 reset primitive so the
 * parser does not require architecture-specific reset knowledge.
 */
static void reboot(void *context) {
    (void)context;
    microflow_rp2040_reboot();
}

/*
 * Copy callbacks and the firmware context into the console so later command
 * dispatch can reach this instance after this initializer returns.
 */
void microflow_firmware_console_initialize(microflow_firmware_t *firmware) {
    const microflow_console_operations_t operations = {
        .context = firmware, .write_response = response, .write_log = log_message, .info = info, .status = status, .tasks = tasks, .queues = queues, .flow_show = flow_show, .flow_validate = flow_validate, .flow_start = flow_start, .flow_stop = flow_stop, .flow_erase_staging = flow_erase, .gpio_list = gpio_list, .gpio_read = gpio_read, .gpio_write = gpio_write, .reboot = reboot
    };
    microflow_console_initialize(&firmware->console,
                                 &firmware->console_receive, &operations);
}
