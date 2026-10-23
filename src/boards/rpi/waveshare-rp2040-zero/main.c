#include <microflow/internal/board.h>
#include "rp2040.h"
#include "supervisor.h"
#include "upload_transport.h"

#include <stdbool.h>
#include <stdint.h>

#define HEARTBEAT_PERIOD_US 500000U
#define WATCHDOG_TIMEOUT_US 2000000U
#define STATUS_SERVICE_BUDGET_US 2000U
#define UART_SERVICE_BUDGET_US 5000U
#define UART_BAUD_RATE 115200U
#define UART_RECEIVE_TIMEOUT_US 1000000U
#define FLOW_IMAGE_HEADER_SIZE 16U

static microflow_supervisor_t supervisor;
static microflow_byte_ring_t uart_receive;
static microflow_byte_ring_t uart_transmit;
static microflow_upload_transport_t upload_transport;
static volatile uint8_t active_flow[MICROFLOW_UPLOAD_MAX_IMAGE_SIZE];
static volatile size_t active_flow_length;

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
void microflow_rp2040_usb_event(void) { microflow_supervisor_signal_from_isr(&supervisor, MICROFLOW_SUPERVISOR_EVENT_USB); }

int main(void) {
  microflow_board_initialize();
  microflow_byte_ring_initialize(&uart_receive);
  microflow_byte_ring_initialize(&uart_transmit);
  microflow_upload_transport_initialize(&upload_transport, &uart_receive,
      &uart_transmit, validate_flow, activate_flow, NULL);
  microflow_rp2040_watchdog_initialize(WATCHDOG_TIMEOUT_US);
  const microflow_supervisor_platform_t platform = {
      .now = platform_now, .wait_until = platform_wait,
      .watchdog_service = platform_watchdog, .fault_report = platform_fault,
      .critical_enter = platform_critical_enter, .critical_exit = platform_critical_exit,
      .context = NULL};
  if (!microflow_supervisor_initialize(&supervisor, &platform, HEARTBEAT_PERIOD_US) ||
      !microflow_supervisor_add_service(&supervisor, 10U,
          MICROFLOW_SUPERVISOR_EVENT_TIMER, STATUS_SERVICE_BUDGET_US,
          status_service, NULL) ||
      !microflow_supervisor_add_service(&supervisor, 2U,
          MICROFLOW_SUPERVISOR_EVENT_UART0_RX | MICROFLOW_SUPERVISOR_EVENT_UART0_TX,
          UART_SERVICE_BUDGET_US, uart_service, NULL)) {
    for (;;) { microflow_board_status_rgb(16U, 0U, 0U); microflow_board_delay_ms(500U); }
  }
  microflow_rp2040_uart0_initialize(UART_BAUD_RATE);
  microflow_supervisor_run(&supervisor);
}
