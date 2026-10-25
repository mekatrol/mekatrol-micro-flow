/* Raspberry Pi Pico firmware composition root and service wiring. */
#include <microflow/internal/board.h>
#include "rp2040_sdk.h"
#include "rp2040_usb_cdc_sdk.h"
#include "board_gpio.h"
#include "supervisor.h"
#include "upload_transport.h"
#include "console.h"

#include <stdbool.h>
#include <stdint.h>

/* Period between visible status changes and timer service releases. */
#define HEARTBEAT_PERIOD_US      500000U
/* Maximum unserviced interval before hardware resets stalled firmware. */
#define WATCHDOG_TIMEOUT_US      2000000U
/* Cooperative maximum intended execution time for status work. */
#define STATUS_SERVICE_BUDGET_US 2000U
/* Cooperative maximum intended execution time for UART work. */
#define UART_SERVICE_BUDGET_US   5000U
/* Conventional host-compatible debug/upload UART bit rate. */
#define UART_BAUD_RATE           115200U
/* Abandon incomplete UART frames after one second without input. */
#define UART_RECEIVE_TIMEOUT_US  1000000U
/* Minimum byte count accepted by the current flow-envelope validator. */
#define FLOW_IMAGE_HEADER_SIZE   16U
/* Cooperative maximum intended execution time for USB CDC work. */
#define USB_SERVICE_BUDGET_US    5000U
/* Fixed per-direction USB queue size; prevents dynamic allocation. */
#define USB_QUEUE_CAPACITY       256U

/* Static state keeps the firmware's RAM requirements link-time measurable. */
static microflow_supervisor_t       supervisor;
static microflow_byte_ring_t        uart_receive;
static microflow_byte_ring_t        uart_transmit;
static microflow_upload_transport_t upload_transport;
static volatile uint8_t             active_flow[MICROFLOW_UPLOAD_MAX_IMAGE_SIZE];
static volatile size_t              active_flow_length;
static bool                         flow_running;
static microflow_rp2040_usb_cdc_t   usb_cdc;
static uint8_t                      usb_driver_receive[USB_QUEUE_CAPACITY];
static uint8_t                      usb_driver_transmit[USB_QUEUE_CAPACITY];
static microflow_byte_ring_t        console_receive;
static microflow_console_t          console;
static microflow_board_gpio_t       board_gpio;

static bool validate_flow(void *context, const uint8_t *image, size_t length);

/*
 * Write a NUL-terminated constant or locally assembled string to USB CDC.
 * This is the common text primitive for console responses; it intentionally
 * avoids printf so firmware size, stack use, and execution time stay bounded.
 */
static void usb_write_text(const char *text) {
    /* Find the exact wire length; CDC writes are byte spans, not C strings. */
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    /* Best-effort delivery keeps diagnostics from blocking real-time services. */
    (void)microflow_rp2040_usb_cdc_write(&usb_cdc, (const uint8_t *)text, length);
}

/*
 * Render one unsigned diagnostic counter in decimal on USB CDC.
 * Human-readable counters are needed by the status commands, while reverse
 * digit collection avoids division helpers, formatting state, and allocation.
 */
static void usb_write_u32(uint32_t value) {
    /* Ten bytes hold every decimal uint32 without a sign or terminator. */
    uint8_t digits[10];
    size_t  used = 0U;
    /* Extract least-significant digits first; do-while renders zero as "0". */
    do {
        digits[used++] = (uint8_t)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    /* Reverse the temporary digits to emit conventional most-significant-first text. */
    while (used != 0U) (void)microflow_rp2040_usb_cdc_write(&usb_cdc, &digits[--used], 1U);
}

/* Route synchronous command replies from the generic console to USB CDC. */
static void console_response(void *context, const uint8_t *data, size_t length) {
    /* Console output is best-effort; CDC metrics expose any dropped suffix. */
    (void)context;
    (void)microflow_rp2040_usb_cdc_write(&usb_cdc, data, length);
}

/* Prefix asynchronous diagnostic output so hosts can distinguish it from replies. */
static void console_log(void *context, const uint8_t *data, size_t length) {
    (void)context;
    usb_write_text("LOG ");
    (void)microflow_rp2040_usb_cdc_write(&usb_cdc, data, length);
}

/* Identify this firmware and console protocol to a connected operator or tool. */
static void console_info(void *context) {
    (void)context;
    usb_write_text("OK Micro Flow Raspberry Pi Pico CDC v1\r\n");
}

/* Summarize the active flow lifecycle, image size, and accumulated faults. */
static void console_status(void *context) {
    /* Report lifecycle and fault state without exposing internal pointers. */
    (void)context;
    usb_write_text("OK flow=");
    usb_write_text(flow_running ? "running" : "stopped");
    usb_write_text(" active-bytes=");
    usb_write_u32((uint32_t)active_flow_length);
    usb_write_text(" faults=");
    usb_write_u32(supervisor.fault_count);
    usb_write_text("\r\n");
}

/* Expose supervisor liveness and per-service runtime metrics for diagnosis. */
static void console_tasks(void *context) {
    /* Per-service counts/maxima make timing budgets observable in the field. */
    (void)context;
    usb_write_text("OK services=");
    usb_write_u32((uint32_t)supervisor.service_count);
    usb_write_text(" heartbeat=");
    usb_write_u32(supervisor.heartbeat);
    usb_write_text(" watchdog=");
    usb_write_u32(supervisor.watchdog_service_count);
    for (size_t index = 0U; index < supervisor.service_count; ++index) {
        usb_write_text(" s");
        usb_write_u32((uint32_t)index);
        usb_write_text("-runs=");
        usb_write_u32(supervisor.services[index].dispatch_count);
        usb_write_text("-max-us=");
        usb_write_u32(supervisor.services[index].observed_maximum_runtime);
    }
    usb_write_text("\r\n");
}

/* Expose queue pressure and data loss so static capacities can be evaluated. */
static void console_queues(void *context) {
    /* High-water and rejection counters reveal insufficient static capacity. */
    (void)context;
    usb_write_text("OK uart-rx-high=");
    usb_write_u32(uart_receive.high_water);
    usb_write_text(" uart-tx-high=");
    usb_write_u32(uart_transmit.high_water);
    usb_write_text(" uart-rx-dropped=");
    usb_write_u32(uart_receive.rejected);
    usb_write_text(" uart-tx-dropped=");
    usb_write_u32(uart_transmit.rejected);
    usb_write_text(" usb-rx-high=");
    usb_write_u32(usb_cdc.receive_high_water);
    usb_write_text(" usb-tx-high=");
    usb_write_u32(usb_cdc.transmit_high_water);
    usb_write_text(" usb-rx-dropped=");
    usb_write_u32(usb_cdc.receive_dropped);
    usb_write_text(" usb-tx-dropped=");
    usb_write_u32(usb_cdc.transmit_dropped);
    usb_write_text("\r\n");
}

/* Report active and staged image sizes without dumping executable flow bytes. */
static void console_flow_show(void *context) {
    (void)context;
    usb_write_text("OK active-bytes=");
    usb_write_u32((uint32_t)active_flow_length);
    usb_write_text(" staging-bytes=");
    usb_write_u32(upload_transport.received_length);
    usb_write_text("\r\n");
}

/* Recheck the active flow envelope before an operator attempts execution. */
static bool console_flow_validate(void *context) {
    /* An empty active slot is not a valid executable flow. */
    (void)context;
    return active_flow_length != 0U &&
           validate_flow(NULL, (const uint8_t *)(uintptr_t)active_flow, active_flow_length);
}

/* Enter running state only when the currently active image remains valid. */
static bool console_flow_start(void *context) {
    (void)context;
    /* Revalidate immediately before changing lifecycle state. */
    if (!console_flow_validate(NULL)) return false;
    flow_running = true;
    return true;
}

/* Stop flow execution and immediately return all logical GPIO to safe inputs. */
static bool console_flow_stop(void *context) {
    (void)context;
    /* A stopped flow must not leave physical outputs asserted. */
    flow_running = false;
    microflow_board_gpio_reset_safe(&board_gpio);
    return true;
}

/* Clear only the upload staging area; the separately active image is preserved. */
static void console_flow_erase(void *context) {
    (void)context;
    microflow_upload_transport_erase_staging(&upload_transport);
}

/* Advertise logical resources rather than allowing discovery of physical pins. */
static void console_gpio_list(void *context) {
    (void)context;
    usb_write_text("OK 0 1 2 3 status-led\r\n");
}

/* Read a validated logical GPIO through the board-owned resource mapping. */
static bool console_gpio_read(void *context, uint32_t resource, bool *value) {
    (void)context;
    return microflow_board_gpio_read(&board_gpio, resource, value);
}

/* Configure and drive one logical GPIO while preserving board pin abstraction. */
static bool console_gpio_write(void *context, uint32_t resource, bool value) {
    (void)context;
    /* Console writes deliberately opt the logical resource into output mode. */
    if (!microflow_board_gpio_configure(
            &board_gpio, resource, MICROFLOW_BOARD_GPIO_OUTPUT)) {
        return false;
    }
    return microflow_board_gpio_write(&board_gpio, resource, value);
}

/* Translate the console's reboot command into the RP2040 reset mechanism. */
static void console_reboot(void *context) {
    (void)context;
    microflow_rp2040_reboot();
}

/* Immutable command routing table is copied into console state at startup. */
static const microflow_console_operations_t console_operations = {
    .context = NULL, .write_response = console_response, .write_log = console_log, .info = console_info, .status = console_status, .tasks = console_tasks, .queues = console_queues, .flow_show = console_flow_show, .flow_validate = console_flow_validate, .flow_start = console_flow_start, .flow_stop = console_flow_stop, .flow_erase_staging = console_flow_erase, .gpio_list = console_gpio_list, .gpio_read = console_gpio_read, .gpio_write = console_gpio_write, .reboot = console_reboot};

/* Convert a project GPIO direction into the corresponding RP2040 SDK setup. */
static void board_gpio_configure(
    void *context, uint32_t pin, microflow_board_gpio_direction_t direction) {
    (void)context;
    /* Translate project-owned directions only at the board/SoC boundary. */
    if (direction == MICROFLOW_BOARD_GPIO_OUTPUT) {
        microflow_rp2040_gpio_output_initialize(pin);
    } else {
        microflow_rp2040_gpio_input_initialize(pin);
    }
}

/* Sample a physical pin for the logical-GPIO adapter without leaking SDK types. */
static bool board_gpio_read_pin(void *context, uint32_t pin) {
    (void)context;
    return microflow_rp2040_gpio_read(pin);
}

/* Drive a physical pin on behalf of the validated logical-GPIO adapter. */
static void board_gpio_write_pin(void *context, uint32_t pin, bool value) {
    (void)context;
    microflow_rp2040_gpio_write(pin, value);
}

/* Driver table keeps physical GPIO access out of the logical resource module. */
static const microflow_board_gpio_driver_t board_gpio_driver = {
    .context   = NULL,
    .configure = board_gpio_configure,
    .read      = board_gpio_read_pin,
    .write     = board_gpio_write_pin};

/* Decode a flow-image uint32 independently of CPU alignment and byte order. */
static uint32_t image_u32(const uint8_t *p) {
    /* Flow image integers use an architecture-independent little-endian form. */
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

/*
 * Validate the complete portable flow envelope before it can be activated or
 * started.  Structural fields are checked before the payload CRC so malformed
 * lengths can never cause an out-of-bounds checksum read.
 */
static bool validate_flow(void *context, const uint8_t *image, size_t length) {
    (void)context;
    /* Validate envelope magic/version/header/length before payload access. */
    if ((length < FLOW_IMAGE_HEADER_SIZE) || (image[0] != 'M') ||
        (image[1] != 'F') || (image[2] != 'F') || (image[3] != 'L') ||
        (image[4] != 1U) || (image[5] != 0U) ||
        (image[6] != FLOW_IMAGE_HEADER_SIZE) || (image[7] != 0U) ||
        (image_u32(&image[8]) != length)) return false;
    /* Payload CRC detects corruption beyond the structurally checked header. */
    return image_u32(&image[12]) ==
           microflow_upload_crc32(&image[FLOW_IMAGE_HEADER_SIZE], length - FLOW_IMAGE_HEADER_SIZE);
}

/*
 * Atomically replace the active flow with a previously validated staged image.
 * Copying into fixed storage decouples execution from mutable upload buffers.
 */
static bool activate_flow(void *context, const uint8_t *image, size_t length) {
    (void)context;
    if (length > sizeof(active_flow)) return false;
    /* Publish bytes and length atomically with respect to interrupt readers. */
    const uint32_t state = microflow_rp2040_interrupt_lock();
    for (size_t i = 0U; i < length; ++i) active_flow[i] = image[i];
    active_flow_length = length;
    microflow_rp2040_interrupt_unlock(state);
    return true;
}

/* Supply the supervisor with the board's wrapping monotonic microsecond clock. */
static uint32_t platform_now(void *context) {
    (void)context;
    return microflow_board_monotonic_ticks();
}

/* Idle the CPU until an interrupt or the next supervisor deadline can wake it. */
static void platform_wait(void *context, bool has_deadline, uint32_t deadline) {
    (void)context;
    microflow_rp2040_wait_until(&supervisor.pending_events, has_deadline, deadline);
}

/* Prove main-loop progress by refreshing the hardware watchdog once per pass. */
static void platform_watchdog(void *context) {
    (void)context;
    microflow_rp2040_watchdog_service();
}

/* Begin the short interrupt exclusion used to snapshot shared event flags. */
static uint32_t platform_critical_enter(void *context) {
    (void)context;
    return microflow_rp2040_interrupt_lock();
}

/* Restore the exact interrupt state captured on critical-section entry. */
static void platform_critical_exit(void *context, uint32_t state) {
    (void)context;
    microflow_rp2040_interrupt_unlock(state);
}

/* Convert any supervisor fault into a simple persistent red hardware signal. */
static void platform_fault(void *context, microflow_supervisor_fault_t fault) {
    (void)context;
    (void)fault;
    /* Red is a persistent, board-independent indication of degraded operation. */
    microflow_board_status_rgb(16U, 0U, 0U);
}

/* Provide a heartbeat and expire stalled uploads on each periodic timer event. */
static void status_service(void *context, uint32_t events) {
    /* Timer work combines visible liveness with stalled-upload expiration. */
    static bool illuminated;
    (void)context;
    (void)events;
    illuminated = !illuminated;
    (void)microflow_upload_transport_expire(&upload_transport,
                                            microflow_board_monotonic_ticks(), UART_RECEIVE_TIMEOUT_US);
    microflow_board_status_rgb(0U, illuminated ? 12U : 0U, 0U);
}

/* Parse queued UART upload bytes and arm TX interrupts for generated replies. */
static void uart_service(void *context, uint32_t events) {
    (void)context;
    (void)events;
    /* Parse outside interrupt context, then enable TX IRQ only when needed. */
    microflow_upload_transport_service(&upload_transport,
                                       microflow_board_monotonic_ticks());
    microflow_rp2040_uart0_set_transmit_interrupt(
        microflow_byte_ring_count(&uart_transmit) != 0U);
}

/* Poll USB CDC, bridge received bytes, and run console parsing in main context. */
static void usb_service(void *context, uint32_t events) {
    (void)context;
    (void)events;
    uint8_t value;
    /* Bridge TinyUSB's private ring into the console's supervisor byte ring. */
    microflow_rp2040_usb_cdc_task(&usb_cdc);
    while (microflow_rp2040_usb_cdc_read(&usb_cdc, &value))
        (void)microflow_supervisor_byte_ring_push_from_isr(&supervisor, &console_receive, value);
    microflow_console_service(&console);
}

/* Defer timer-alarm work from the SoC ISR into the cooperative supervisor. */
void microflow_rp2040_timer_alarm0_event(void) {
    /* ISR hook only coalesces work; service execution stays in main context. */
    microflow_supervisor_signal_from_isr(&supervisor, MICROFLOW_SUPERVISOR_EVENT_TIMER);
}

/* Drain/fill UART hardware FIFOs in interrupt context and signal resulting work. */
void microflow_rp2040_uart0_event(void) {
    /* Bound ISR work by finite hardware FIFOs and preallocated software rings. */
    uint32_t events = 0U;
    while (microflow_rp2040_uart0_read_ready()) {
        (void)microflow_supervisor_byte_ring_push_from_isr(
            &supervisor, &uart_receive, microflow_rp2040_uart0_read());
        events |= MICROFLOW_SUPERVISOR_EVENT_UART0_RX;
    }
    uint8_t value;
    while (microflow_rp2040_uart0_write_ready() &&
           microflow_byte_ring_pop(&uart_transmit, &value)) {
        microflow_rp2040_uart0_write(value);
        events |= MICROFLOW_SUPERVISOR_EVENT_UART0_TX;
    }
    if (microflow_byte_ring_count(&uart_transmit) == 0U)
        microflow_rp2040_uart0_set_transmit_interrupt(false);
    if (events != 0U) microflow_supervisor_signal_from_isr(&supervisor, events);
}

/* Coalesce a USB controller interrupt into one pending supervisor event bit. */
void microflow_rp2040_usb_event(void) {
    /* USB interrupt details remain in TinyUSB; the supervisor receives one bit. */
    microflow_supervisor_signal_from_isr(&supervisor, MICROFLOW_SUPERVISOR_EVENT_USB);
}

/*
 * Compose all board services, validate required capabilities, then hand control
 * to the non-returning cooperative supervisor.  Initialization order ensures
 * no interrupt can observe an uninitialized queue or service object.
 */
int main(void) {
    /* Initialize dependencies from hardware outward before enabling services. */
    microflow_board_initialize();
    microflow_board_gpio_initialize(&board_gpio, &board_gpio_driver);
    microflow_byte_ring_initialize(&uart_receive);
    microflow_byte_ring_initialize(&uart_transmit);
    microflow_byte_ring_initialize(&console_receive);
    microflow_upload_transport_initialize(&upload_transport, &uart_receive,
                                          &uart_transmit, validate_flow, activate_flow, NULL);
    microflow_rp2040_watchdog_initialize(WATCHDOG_TIMEOUT_US);
    const microflow_supervisor_platform_t platform = {
        .now = platform_now, .wait_until = platform_wait, .watchdog_service = platform_watchdog, .fault_report = platform_fault, .critical_enter = platform_critical_enter, .critical_exit = platform_critical_exit, .context = NULL};
    microflow_console_initialize(&console, &console_receive, &console_operations);
    /* Refuse normal operation unless every required service can be registered. */
    if (!microflow_supervisor_initialize(&supervisor, &platform, HEARTBEAT_PERIOD_US) ||
        !microflow_supervisor_add_service(&supervisor, 10U,
                                          MICROFLOW_SUPERVISOR_EVENT_TIMER, STATUS_SERVICE_BUDGET_US,
                                          status_service, NULL) ||
        !microflow_supervisor_add_service(&supervisor, 2U,
                                          MICROFLOW_SUPERVISOR_EVENT_UART0_RX | MICROFLOW_SUPERVISOR_EVENT_UART0_TX,
                                          UART_SERVICE_BUDGET_US, uart_service, NULL) ||
        !microflow_supervisor_add_service(&supervisor, 1U,
                                          MICROFLOW_SUPERVISOR_EVENT_USB, USB_SERVICE_BUDGET_US, usb_service, NULL)) {
        for (;;) {
            /* A red half-second blink is the allocation-free fatal fallback. */
            microflow_board_status_rgb(16U, 0U, 0U);
            microflow_board_delay_ms(500U);
        }
    }
    /* Peripheral interrupts start only after their supervisor state exists. */
    microflow_rp2040_uart0_initialize(UART_BAUD_RATE);
    if (!microflow_rp2040_usb_cdc_initialize(&usb_cdc, usb_driver_receive,
                                             sizeof(usb_driver_receive), usb_driver_transmit, sizeof(usb_driver_transmit)))
        microflow_supervisor_report_fault(&supervisor,
                                          MICROFLOW_SUPERVISOR_FAULT_CONFIGURATION);
    /* The cooperative supervisor owns control for the remainder of execution. */
    microflow_supervisor_run(&supervisor);
}
