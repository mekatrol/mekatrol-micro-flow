/*
 * Adapt Pico time, sleep, watchdog, fault, and UART operations to supervisor
 * callbacks so supervisor.c contains no RP2040 dependencies.
 */
#include "firmware_internal.h"

#include <microflow/internal/board.h>

#include "rp2040_sdk.h"

/* Supply the supervisor with the board's wrapping monotonic tick counter. */
static uint32_t now(void *context) {
    (void)context;
    return microflow_board_monotonic_ticks();
}

/* Idle until an interrupt or the next scheduled deadline can wake the CPU. */
static void wait_until(void *context, bool has_deadline, uint32_t deadline) {
    microflow_firmware_t *f = context;
    microflow_rp2040_wait_until(&f->supervisor.pending_events, has_deadline,
                                deadline);
}

/* Prove main-loop progress by refreshing the hardware watchdog once per pass. */
static void watchdog(void *context) {
    (void)context;
    microflow_rp2040_watchdog_service();
}

/* Begin the short interrupt exclusion used when publishing shared state. */
uint32_t microflow_firmware_lock(void *context) {
    (void)context;
    return microflow_rp2040_interrupt_lock();
}

/* Restore exactly the interrupt state captured on critical-section entry. */
void microflow_firmware_unlock(void *context, uint32_t state) {
    (void)context;
    microflow_rp2040_interrupt_unlock(state);
}

/* Convert any supervisor fault into a persistent red hardware indication. */
static void fault(void *context, microflow_supervisor_fault_t value) {
    (void)context;
    (void)value;
    /* The indication is deliberately simple and independent of USB/UART health. */
    microflow_board_status_rgb(16U, 0U, 0U);
}

/* Assemble the board-specific callbacks consumed by the portable supervisor. */
microflow_supervisor_platform_t
microflow_firmware_platform(microflow_firmware_t *firmware) {
    return (microflow_supervisor_platform_t){
        .now = now, .wait_until = wait_until, .watchdog_service = watchdog, .fault_report = fault, .critical_enter = microflow_firmware_lock, .critical_exit = microflow_firmware_unlock, .context = firmware
    };
}

/* Enforce safe physical outputs whenever the active flow stops running. */
void microflow_firmware_flow_stopped(void *context) {
    microflow_firmware_t *f = context;
    microflow_board_gpio_reset_safe(&f->gpio);
}

/* Update visible liveness and expire incomplete uploads on each timer event. */
void microflow_firmware_status_service(void *context, uint32_t events) {
    microflow_firmware_t *f = context;
    static bool           illuminated;
#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
    static uint8_t       colour;
    static const uint8_t colours[][3] = {
        { 12U, 0U, 0U }, { 0U, 12U, 0U }, { 0U, 0U, 12U }
    };
#endif
    (void)events;
    /* Timer work combines the heartbeat with stalled-upload housekeeping. */
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

/* Parse queued upload bytes outside IRQ context and arm replies when needed. */
void microflow_firmware_uart_service(void *context, uint32_t events) {
    microflow_firmware_t *f = context;
    (void)events;
    /* Parsing may validate an image, so it belongs in bounded foreground work. */
    microflow_upload_transport_service(&f->upload,
                                       microflow_board_monotonic_ticks());
    microflow_rp2040_uart0_set_transmit_interrupt(
        microflow_byte_ring_count(&f->uart_transmit) != 0U);
}
