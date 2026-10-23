#include <microflow/internal/board.h>
#include "rp2040.h"
#include "supervisor.h"

#include <stdbool.h>
#include <stdint.h>

#define HEARTBEAT_PERIOD_US 500000U
#define WATCHDOG_TIMEOUT_US 2000000U
#define STATUS_SERVICE_BUDGET_US 2000U

static microflow_supervisor_t supervisor;

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
  microflow_board_status_rgb(0U, illuminated ? 12U : 0U, 0U);
}

void microflow_rp2040_timer_alarm0_event(void) { microflow_supervisor_signal_from_isr(&supervisor, MICROFLOW_SUPERVISOR_EVENT_TIMER); }
void microflow_rp2040_uart0_event(void) { microflow_supervisor_signal_from_isr(&supervisor, MICROFLOW_SUPERVISOR_EVENT_UART0_RX); }
void microflow_rp2040_usb_event(void) { microflow_supervisor_signal_from_isr(&supervisor, MICROFLOW_SUPERVISOR_EVENT_USB); }

int main(void) {
  microflow_board_initialize();
  microflow_rp2040_watchdog_initialize(WATCHDOG_TIMEOUT_US);
  const microflow_supervisor_platform_t platform = {
      .now = platform_now, .wait_until = platform_wait,
      .watchdog_service = platform_watchdog, .fault_report = platform_fault,
      .critical_enter = platform_critical_enter, .critical_exit = platform_critical_exit,
      .context = NULL};
  if (!microflow_supervisor_initialize(&supervisor, &platform, HEARTBEAT_PERIOD_US) ||
      !microflow_supervisor_add_service(&supervisor, 10U,
          MICROFLOW_SUPERVISOR_EVENT_TIMER, STATUS_SERVICE_BUDGET_US,
          status_service, NULL)) {
    for (;;) { microflow_board_status_rgb(16U, 0U, 0U); microflow_board_delay_ms(500U); }
  }
  microflow_supervisor_run(&supervisor);
}
