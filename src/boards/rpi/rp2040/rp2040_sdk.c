#include "rp2040_sdk.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#define MICROFLOW_UART uart0
#define MICROFLOW_UART_IRQ UART0_IRQ
#define MICROFLOW_UART_TX_PIN 0U
#define MICROFLOW_UART_RX_PIN 1U

static bool uart_transmit_interrupt_enabled;

static void alarm0_callback(uint alarm_number) {
  (void)alarm_number;
  microflow_rp2040_timer_alarm0_event();
}

static void uart0_irq_handler(void) { microflow_rp2040_uart0_event(); }

void microflow_rp2040_initialize(void) {
  hardware_alarm_claim(0U);
  hardware_alarm_set_callback(0U, alarm0_callback);
}

uint32_t microflow_rp2040_monotonic_microseconds(void) { return time_us_32(); }

void microflow_rp2040_gpio_output_initialize(uint32_t pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
}

void microflow_rp2040_gpio_input_initialize(uint32_t pin) {
  gpio_init(pin);
  gpio_disable_pulls(pin);
  gpio_set_dir(pin, GPIO_IN);
}

bool microflow_rp2040_gpio_read(uint32_t pin) { return gpio_get(pin); }

void microflow_rp2040_gpio_write(uint32_t pin, bool high) {
  gpio_put(pin, high);
}

void microflow_rp2040_ws2812_initialize(uint32_t pin) {
  microflow_rp2040_gpio_output_initialize(pin);
  gpio_put(pin, false);
}

void microflow_rp2040_ws2812_write(uint32_t pin, uint8_t red, uint8_t green,
                                   uint8_t blue) {
  const uint32_t value = ((uint32_t)green << 16U) | ((uint32_t)red << 8U) | blue;
  const uint32_t state = save_and_disable_interrupts();
  for (uint32_t mask = 1UL << 23U; mask != 0U; mask >>= 1U) {
    gpio_put(pin, true);
    busy_wait_at_least_cycles((value & mask) != 0U ? 88U : 38U);
    gpio_put(pin, false);
    busy_wait_at_least_cycles((value & mask) != 0U ? 50U : 100U);
  }
  restore_interrupts(state);
  busy_wait_us_32(80U);
}

void microflow_rp2040_delay_ms(uint32_t milliseconds) { sleep_ms(milliseconds); }

void microflow_rp2040_wait_until(const volatile uint32_t *wake_events,
                                 bool has_deadline, uint32_t deadline) {
  uint32_t state = save_and_disable_interrupts();
  if ((*wake_events == 0U) && has_deadline) {
    const int32_t remaining = (int32_t)(deadline - time_us_32());
    if (remaining > 0) {
      (void)hardware_alarm_set_target(0U, make_timeout_time_us((uint32_t)remaining));
    }
  }
  if ((*wake_events == 0U) &&
      (!has_deadline || ((int32_t)(deadline - time_us_32()) > 0))) {
    __wfi();
  }
  restore_interrupts(state);
  hardware_alarm_cancel(0U);
  microflow_rp2040_usb_event();
}

void microflow_rp2040_watchdog_initialize(uint32_t timeout_microseconds) {
  watchdog_enable((timeout_microseconds + 999U) / 1000U, true);
}

void microflow_rp2040_watchdog_service(void) { watchdog_update(); }

void microflow_rp2040_reboot(void) {
  watchdog_reboot(0U, 0U, 0U);
  for (;;) { tight_loop_contents(); }
}

void microflow_rp2040_uart0_initialize(uint32_t baud_rate) {
  (void)uart_init(MICROFLOW_UART, baud_rate);
  gpio_set_function(MICROFLOW_UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(MICROFLOW_UART_RX_PIN, GPIO_FUNC_UART);
  uart_set_hw_flow(MICROFLOW_UART, false, false);
  uart_set_format(MICROFLOW_UART, 8U, 1U, UART_PARITY_NONE);
  uart_set_fifo_enabled(MICROFLOW_UART, true);
  irq_set_exclusive_handler(MICROFLOW_UART_IRQ, uart0_irq_handler);
  irq_set_enabled(MICROFLOW_UART_IRQ, true);
  uart_set_irq_enables(MICROFLOW_UART, true, false);
}

bool microflow_rp2040_uart0_read_ready(void) { return uart_is_readable(MICROFLOW_UART); }
uint8_t microflow_rp2040_uart0_read(void) { return (uint8_t)uart_getc(MICROFLOW_UART); }
bool microflow_rp2040_uart0_write_ready(void) { return uart_is_writable(MICROFLOW_UART); }
void microflow_rp2040_uart0_write(uint8_t value) { uart_putc_raw(MICROFLOW_UART, (char)value); }

void microflow_rp2040_uart0_set_transmit_interrupt(bool enabled) {
  uart_transmit_interrupt_enabled = enabled;
  uart_set_irq_enables(MICROFLOW_UART, true, uart_transmit_interrupt_enabled);
}

uint32_t microflow_rp2040_interrupt_lock(void) { return save_and_disable_interrupts(); }
void microflow_rp2040_interrupt_unlock(uint32_t state) { restore_interrupts(state); }
