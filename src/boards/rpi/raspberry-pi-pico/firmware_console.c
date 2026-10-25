/* Firmware console commands, kept separate from parsing and USB transport. */
#include "firmware_internal.h"

#include "rp2040_sdk.h"

static void text(microflow_firmware_t *f, const char *value) {
    microflow_firmware_usb_write_text(f, value);
}

static void number(microflow_firmware_t *f, uint32_t value) {
    microflow_firmware_usb_write_u32(f, value);
}

static void response(void *context, const uint8_t *data, size_t length) {
    microflow_firmware_usb_write(context, data, length);
}

static void log_message(void *context, const uint8_t *data, size_t length) {
    microflow_firmware_t *f = context;
    text(f, "LOG ");
    microflow_firmware_usb_write(f, data, length);
}

static void info(void *context) {
    text(context, "OK Micro Flow Raspberry Pi Pico CDC v1\r\n");
}

static void status(void *context) {
    microflow_firmware_t *f = context;
    text(f, "OK flow=");
    text(f, f->flow_store.running ? "running" : "stopped");
    text(f, " active-bytes=");
    number(f, (uint32_t)f->flow_store.length);
    text(f, " faults=");
    number(f, f->supervisor.fault_count);
    text(f, "\r\n");
}

static void tasks(void *context) {
    microflow_firmware_t *f = context;
    text(f, "OK services=");
    number(f, (uint32_t)f->supervisor.service_count);
    text(f, " heartbeat=");
    number(f, f->supervisor.heartbeat);
    text(f, " watchdog=");
    number(f, f->supervisor.watchdog_service_count);
    for (size_t i = 0U; i < f->supervisor.service_count; ++i) {
        text(f, " s");
        number(f, (uint32_t)i);
        text(f, "-runs=");
        number(f, f->supervisor.services[i].dispatch_count);
        text(f, "-max-us=");
        number(f, f->supervisor.services[i].observed_maximum_runtime);
    }
    text(f, "\r\n");
}

static void queues(void *context) {
    microflow_firmware_t *f = context;
    text(f, "OK uart-rx-high=");
    number(f, f->uart_receive.high_water);
    text(f, " uart-tx-high=");
    number(f, f->uart_transmit.high_water);
    text(f, " uart-rx-dropped=");
    number(f, f->uart_receive.rejected);
    text(f, " uart-tx-dropped=");
    number(f, f->uart_transmit.rejected);
    text(f, " usb-rx-high=");
    number(f, f->usb.receive_high_water);
    text(f, " usb-tx-high=");
    number(f, f->usb.transmit_high_water);
    text(f, " usb-rx-dropped=");
    number(f, f->usb.receive_dropped);
    text(f, " usb-tx-dropped=");
    number(f, f->usb.transmit_dropped);
    text(f, "\r\n");
}

static void flow_show(void *context) {
    microflow_firmware_t *f = context;
    text(f, "OK active-bytes=");
    number(f, (uint32_t)f->flow_store.length);
    text(f, " staging-bytes=");
    number(f, f->upload.received_length);
    text(f, "\r\n");
}

static bool flow_validate(void *context) {
    microflow_firmware_t *f = context;
    return microflow_flow_store_validate(&f->flow_store);
}

static bool flow_start(void *context) {
    microflow_firmware_t *f = context;
    return microflow_flow_store_start(&f->flow_store);
}

static bool flow_stop(void *context) {
    microflow_firmware_t *f = context;
    microflow_flow_store_stop(&f->flow_store);
    return true;
}

static void flow_erase(void *context) {
    microflow_firmware_t *f = context;
    microflow_upload_transport_erase_staging(&f->upload);
}

static void gpio_list(void *context) {
    text(context, "OK 0 1 2 3 status-led\r\n");
}

static bool gpio_read(void *context, uint32_t resource, bool *value) {
    microflow_firmware_t *f = context;
    return microflow_board_gpio_read(&f->gpio, resource, value);
}

static bool gpio_write(void *context, uint32_t resource, bool value) {
    microflow_firmware_t *f = context;
    return microflow_board_gpio_configure(
               &f->gpio, resource, MICROFLOW_BOARD_GPIO_OUTPUT) &&
           microflow_board_gpio_write(&f->gpio, resource, value);
}

static void reboot(void *context) {
    (void)context;
    microflow_rp2040_reboot();
}

void microflow_firmware_console_initialize(microflow_firmware_t *firmware) {
    const microflow_console_operations_t operations = {
        .context = firmware, .write_response = response, .write_log = log_message, .info = info, .status = status, .tasks = tasks, .queues = queues, .flow_show = flow_show, .flow_validate = flow_validate, .flow_start = flow_start, .flow_stop = flow_stop, .flow_erase_staging = flow_erase, .gpio_list = gpio_list, .gpio_read = gpio_read, .gpio_write = gpio_write, .reboot = reboot};
    microflow_console_initialize(&firmware->console,
                                 &firmware->console_receive, &operations);
}
