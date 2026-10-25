/* Supervisor platform binding and non-USB firmware services. */
#include "firmware_internal.h"

#include <microflow/internal/board.h>

#include "rp2040_sdk.h"

static uint32_t now(void *context) {
    (void)context;
    return microflow_board_monotonic_ticks();
}

static void wait_until(void *context, bool has_deadline, uint32_t deadline) {
    microflow_firmware_t *f = context;
    microflow_rp2040_wait_until(&f->supervisor.pending_events, has_deadline,
                                deadline);
}

static void watchdog(void *context) {
    (void)context;
    microflow_rp2040_watchdog_service();
}

uint32_t microflow_firmware_lock(void *context) {
    (void)context;
    return microflow_rp2040_interrupt_lock();
}

void microflow_firmware_unlock(void *context, uint32_t state) {
    (void)context;
    microflow_rp2040_interrupt_unlock(state);
}

static void fault(void *context, microflow_supervisor_fault_t value) {
    (void)context;
    (void)value;
    microflow_board_status_rgb(16U, 0U, 0U);
}

microflow_supervisor_platform_t
microflow_firmware_platform(microflow_firmware_t *firmware) {
    return (microflow_supervisor_platform_t){
        .now = now, .wait_until = wait_until, .watchdog_service = watchdog, .fault_report = fault, .critical_enter = microflow_firmware_lock, .critical_exit = microflow_firmware_unlock, .context = firmware};
}

void microflow_firmware_flow_stopped(void *context) {
    microflow_firmware_t *f = context;
    microflow_board_gpio_reset_safe(&f->gpio);
}

void microflow_firmware_status_service(void *context, uint32_t events) {
    microflow_firmware_t *f = context;
    static bool           illuminated;
#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
    static uint8_t       colour;
    static const uint8_t colours[][3] = {
        {12U, 0U, 0U}, {0U, 12U, 0U}, {0U, 0U, 12U}};
#endif
    (void)events;
    illuminated = !illuminated;
    (void)microflow_upload_transport_expire(
        &f->upload, microflow_board_monotonic_ticks(),
        MICROFLOW_UART_RECEIVE_TIMEOUT_US);
#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
    if (illuminated) {
        microflow_board_status_rgb(colours[colour][0], colours[colour][1],
                                   colours[colour][2]);
        colour = (uint8_t)((colour + 1U) % 3U);
    } else {
        microflow_board_status_rgb(0U, 0U, 0U);
    }
#else
    microflow_board_status_rgb(0U, illuminated ? 12U : 0U, 0U);
#endif
}

void microflow_firmware_uart_service(void *context, uint32_t events) {
    microflow_firmware_t *f = context;
    (void)events;
    microflow_upload_transport_service(&f->upload,
                                       microflow_board_monotonic_ticks());
    microflow_rp2040_uart0_set_transmit_interrupt(
        microflow_byte_ring_count(&f->uart_transmit) != 0U);
}
