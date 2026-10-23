#include <microflow/internal/board.h>
#include "rp2040_sdk.h"
#include "rp2040_usb_cdc_sdk.h"
#include "supervisor.h"
#include "upload_transport.h"
#include "console.h"

#include <stdbool.h>
#include <stdint.h>

#define HEARTBEAT_PERIOD_US 500000U
#define WATCHDOG_TIMEOUT_US 2000000U
#define STATUS_SERVICE_BUDGET_US 2000U
#define UART_SERVICE_BUDGET_US 5000U
#define UART_BAUD_RATE 115200U
#define UART_RECEIVE_TIMEOUT_US 1000000U
#define FLOW_IMAGE_HEADER_SIZE 16U
#define USB_SERVICE_BUDGET_US 5000U
#define USB_QUEUE_CAPACITY 256U

static microflow_supervisor_t supervisor;
static microflow_byte_ring_t uart_receive;
static microflow_byte_ring_t uart_transmit;
static microflow_upload_transport_t upload_transport;
static volatile uint8_t active_flow[MICROFLOW_UPLOAD_MAX_IMAGE_SIZE];
static volatile size_t active_flow_length;
static bool flow_running;
static microflow_rp2040_usb_cdc_t usb_cdc;
static uint8_t usb_driver_receive[USB_QUEUE_CAPACITY];
static uint8_t usb_driver_transmit[USB_QUEUE_CAPACITY];
static microflow_byte_ring_t console_receive;
static microflow_console_t console;

static bool validate_flow(void *context, const uint8_t *image, size_t length);

static void usb_write_text(const char *text) {
  size_t length = 0U; while (text[length] != '\0') ++length;
  (void)microflow_rp2040_usb_cdc_write(&usb_cdc, (const uint8_t *)text, length);
}
static void usb_write_u32(uint32_t value) {
  uint8_t digits[10]; size_t used = 0U;
  do { digits[used++] = (uint8_t)('0' + value % 10U); value /= 10U; } while (value != 0U);
  while (used != 0U) (void)microflow_rp2040_usb_cdc_write(&usb_cdc, &digits[--used], 1U);
}
static void console_response(void *context, const uint8_t *data, size_t length) {
  (void)context; (void)microflow_rp2040_usb_cdc_write(&usb_cdc, data, length);
}
static void console_log(void *context, const uint8_t *data, size_t length) {
  (void)context; usb_write_text("LOG ");
  (void)microflow_rp2040_usb_cdc_write(&usb_cdc, data, length);
}
static void console_info(void *context) { (void)context; usb_write_text("OK Micro Flow RP2040 Zero CDC v1\r\n"); }
static void console_status(void *context) {
  (void)context; usb_write_text("OK flow="); usb_write_text(flow_running ? "running" : "stopped");
  usb_write_text(" active-bytes="); usb_write_u32((uint32_t)active_flow_length);
  usb_write_text(" faults="); usb_write_u32(supervisor.fault_count); usb_write_text("\r\n");
}
static void console_tasks(void *context) {
  (void)context; usb_write_text("OK services="); usb_write_u32((uint32_t)supervisor.service_count);
  usb_write_text(" heartbeat="); usb_write_u32(supervisor.heartbeat); usb_write_text("\r\n");
}
static void console_queues(void *context) {
  (void)context; usb_write_text("OK uart-rx-high="); usb_write_u32(uart_receive.high_water);
  usb_write_text(" uart-tx-high="); usb_write_u32(uart_transmit.high_water);
  usb_write_text(" usb-rx-high="); usb_write_u32(usb_cdc.receive_high_water);
  usb_write_text(" usb-tx-high="); usb_write_u32(usb_cdc.transmit_high_water);
  usb_write_text(" usb-rx-dropped="); usb_write_u32(usb_cdc.receive_dropped);
  usb_write_text(" usb-tx-dropped="); usb_write_u32(usb_cdc.transmit_dropped); usb_write_text("\r\n");
}
static void console_flow_show(void *context) {
  (void)context; usb_write_text("OK active-bytes="); usb_write_u32((uint32_t)active_flow_length);
  usb_write_text(" staging-bytes="); usb_write_u32(upload_transport.received_length); usb_write_text("\r\n");
}
static bool console_flow_validate(void *context) {
  (void)context; return active_flow_length != 0U &&
      validate_flow(NULL, (const uint8_t *)(uintptr_t)active_flow, active_flow_length);
}
static bool console_flow_start(void *context) {
  (void)context; if (!console_flow_validate(NULL)) return false; flow_running = true; return true;
}
static bool console_flow_stop(void *context) { (void)context; flow_running = false; return true; }
static void console_flow_erase(void *context) { (void)context; microflow_upload_transport_erase_staging(&upload_transport); }
static void console_gpio_list(void *context) { (void)context; usb_write_text("OK none (Stage 4)\r\n"); }
static bool console_gpio_read(void *context, uint32_t resource, bool *value) {
  (void)context; (void)resource; (void)value; return false;
}
static bool console_gpio_write(void *context, uint32_t resource, bool value) {
  (void)context; (void)resource; (void)value; return false;
}
static void console_reboot(void *context) { (void)context; microflow_rp2040_reboot(); }
static const microflow_console_operations_t console_operations = {
    .context = NULL, .write_response = console_response, .write_log = console_log,
    .info = console_info, .status = console_status, .tasks = console_tasks,
    .queues = console_queues, .flow_show = console_flow_show,
    .flow_validate = console_flow_validate, .flow_start = console_flow_start,
    .flow_stop = console_flow_stop, .flow_erase_staging = console_flow_erase,
    .gpio_list = console_gpio_list, .gpio_read = console_gpio_read,
    .gpio_write = console_gpio_write, .reboot = console_reboot};

static uint32_t image_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
         ((uint32_t)p[3] << 24U);
}
static bool validate_flow(void *context, const uint8_t *image, size_t length) {
  (void)context;
  if ((length < FLOW_IMAGE_HEADER_SIZE) || (image[0] != 'M') ||
      (image[1] != 'F') || (image[2] != 'F') || (image[3] != 'L') ||
      (image[4] != 1U) || (image[5] != 0U) ||
      (image[6] != FLOW_IMAGE_HEADER_SIZE) || (image[7] != 0U) ||
      (image_u32(&image[8]) != length)) return false;
  return image_u32(&image[12]) ==
      microflow_upload_crc32(&image[FLOW_IMAGE_HEADER_SIZE], length - FLOW_IMAGE_HEADER_SIZE);
}
static bool activate_flow(void *context, const uint8_t *image, size_t length) {
  (void)context;
  if (length > sizeof(active_flow)) return false;
  const uint32_t state = microflow_rp2040_interrupt_lock();
  for (size_t i = 0U; i < length; ++i) active_flow[i] = image[i];
  active_flow_length = length;
  microflow_rp2040_interrupt_unlock(state);
  return true;
}

static uint32_t platform_now(void *context) { (void)context; return microflow_board_monotonic_ticks(); }
static void platform_wait(void *context, bool has_deadline, uint32_t deadline) {
  (void)context;
  microflow_rp2040_wait_until(&supervisor.pending_events, has_deadline, deadline);
}
static void platform_watchdog(void *context) { (void)context; microflow_rp2040_watchdog_service(); }
static uint32_t platform_critical_enter(void *context) { (void)context; return microflow_rp2040_interrupt_lock(); }
static void platform_critical_exit(void *context, uint32_t state) { (void)context; microflow_rp2040_interrupt_unlock(state); }
static void platform_fault(void *context, microflow_supervisor_fault_t fault) {
  (void)context; (void)fault; microflow_board_status_rgb(16U, 0U, 0U);
}
static void status_service(void *context, uint32_t events) {
  static bool illuminated;
  (void)context; (void)events; illuminated = !illuminated;
  (void)microflow_upload_transport_expire(&upload_transport,
      microflow_board_monotonic_ticks(), UART_RECEIVE_TIMEOUT_US);
  microflow_board_status_rgb(0U, illuminated ? 12U : 0U, 0U);
}
static void uart_service(void *context, uint32_t events) {
  (void)context; (void)events;
  microflow_upload_transport_service(&upload_transport,
      microflow_board_monotonic_ticks());
  microflow_rp2040_uart0_set_transmit_interrupt(
      microflow_byte_ring_count(&uart_transmit) != 0U);
}
static void usb_service(void *context, uint32_t events) {
  (void)context; (void)events; uint8_t value;
  microflow_rp2040_usb_cdc_task(&usb_cdc);
  while (microflow_rp2040_usb_cdc_read(&usb_cdc, &value))
    (void)microflow_supervisor_byte_ring_push_from_isr(&supervisor, &console_receive, value);
  microflow_console_service(&console);
}

void microflow_rp2040_timer_alarm0_event(void) { microflow_supervisor_signal_from_isr(&supervisor, MICROFLOW_SUPERVISOR_EVENT_TIMER); }
void microflow_rp2040_uart0_event(void) {
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
void microflow_rp2040_usb_event(void) {
  microflow_supervisor_signal_from_isr(&supervisor, MICROFLOW_SUPERVISOR_EVENT_USB);
}

int main(void) {
  microflow_board_initialize();
  microflow_byte_ring_initialize(&uart_receive);
  microflow_byte_ring_initialize(&uart_transmit);
  microflow_byte_ring_initialize(&console_receive);
  microflow_upload_transport_initialize(&upload_transport, &uart_receive,
      &uart_transmit, validate_flow, activate_flow, NULL);
  microflow_rp2040_watchdog_initialize(WATCHDOG_TIMEOUT_US);
  const microflow_supervisor_platform_t platform = {
      .now = platform_now, .wait_until = platform_wait,
      .watchdog_service = platform_watchdog, .fault_report = platform_fault,
      .critical_enter = platform_critical_enter, .critical_exit = platform_critical_exit,
      .context = NULL};
  microflow_console_initialize(&console, &console_receive, &console_operations);
  if (!microflow_supervisor_initialize(&supervisor, &platform, HEARTBEAT_PERIOD_US) ||
      !microflow_supervisor_add_service(&supervisor, 10U,
          MICROFLOW_SUPERVISOR_EVENT_TIMER, STATUS_SERVICE_BUDGET_US,
          status_service, NULL) ||
      !microflow_supervisor_add_service(&supervisor, 2U,
          MICROFLOW_SUPERVISOR_EVENT_UART0_RX | MICROFLOW_SUPERVISOR_EVENT_UART0_TX,
          UART_SERVICE_BUDGET_US, uart_service, NULL) ||
      !microflow_supervisor_add_service(&supervisor, 1U,
          MICROFLOW_SUPERVISOR_EVENT_USB, USB_SERVICE_BUDGET_US, usb_service, NULL)) {
    for (;;) { microflow_board_status_rgb(16U, 0U, 0U); microflow_board_delay_ms(500U); }
  }
  microflow_rp2040_uart0_initialize(UART_BAUD_RATE);
  if (!microflow_rp2040_usb_cdc_initialize(&usb_cdc, usb_driver_receive,
      sizeof(usb_driver_receive), usb_driver_transmit, sizeof(usb_driver_transmit)))
    microflow_supervisor_report_fault(&supervisor,
        MICROFLOW_SUPERVISOR_FAULT_CONFIGURATION);
  microflow_supervisor_run(&supervisor);
}
