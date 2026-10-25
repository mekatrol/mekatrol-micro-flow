/* Raspberry Pi Pico SDK implementation of project-owned RP2040 primitives. */
#include "rp2040_sdk.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

/* UART0 is the dedicated interrupt-driven upload and diagnostic port. */
#define MICROFLOW_UART uart0
/* IRQ identity stays beside the UART selection to prevent mismatched wiring. */
#define MICROFLOW_UART_IRQ UART0_IRQ
/* GPIO0 carries UART0 transmit on the maintained Pico board mapping. */
#define MICROFLOW_UART_TX_PIN 0U
/* GPIO1 carries UART0 receive on the maintained Pico board mapping. */
#define MICROFLOW_UART_RX_PIN 1U

/* Tracks TX policy because the SDK exposes enable state only indirectly. */
static bool uart_transmit_interrupt_enabled;

/* Forward SDK alarm-zero expiry into the board-defined supervisor event hook. */
static void alarm0_callback(uint alarm_number) {
  /* Ignore the known alarm number and transfer policy to board code. */
  (void)alarm_number;
  microflow_rp2040_timer_alarm0_event();
}

/* Forward UART0 IRQ work to the board composition layer that owns its queues. */
static void uart0_irq_handler(void) { microflow_rp2040_uart0_event(); }

/* Reserve and connect the hardware alarm used for supervisor deadline wakeups. */
void microflow_rp2040_initialize(void) {
  /* Alarm zero is reserved as the supervisor's next-deadline wake source. */
  hardware_alarm_claim(0U);
  hardware_alarm_set_callback(0U, alarm0_callback);
}

/* Expose the SDK's wrapping microsecond timer using a project-owned scalar type. */
uint32_t microflow_rp2040_monotonic_microseconds(void) { return time_us_32(); }

/* Claim a physical GPIO and enable its output driver for board-controlled use. */
void microflow_rp2040_gpio_output_initialize(uint32_t pin) {
  /* GPIO ownership and direction are established before application use. */
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
}

/* Claim a physical GPIO as an unbiased high-impedance input. */
void microflow_rp2040_gpio_input_initialize(uint32_t pin) {
  /* Electrical bias remains an explicit board-level responsibility. */
  gpio_init(pin);
  gpio_disable_pulls(pin);
  gpio_set_dir(pin, GPIO_IN);
}

/* Sample one physical GPIO without exposing the SDK's GPIO API to board policy. */
bool microflow_rp2040_gpio_read(uint32_t pin) { return gpio_get(pin); }

/* Set the output latch of one physical GPIO through the project boundary. */
void microflow_rp2040_gpio_write(uint32_t pin, bool high) {
  gpio_put(pin, high);
}

/* Configure the board status pin and PIO program needed by its WS2812 pixel. */
void microflow_rp2040_ws2812_initialize(uint32_t pin) {
  microflow_rp2040_gpio_output_initialize(pin);
  gpio_put(pin, false);
}

/* Send one RGB colour using WS2812's GRB wire order and required timing. */
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

/* Provide an explicit millisecond blocking delay for startup/fatal paths only. */
void microflow_rp2040_delay_ms(uint32_t milliseconds) { sleep_ms(milliseconds); }

/* Sleep efficiently until pending work exists or an optional deadline arrives. */
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

/* Start the hardware watchdog with a conservatively rounded timeout. */
void microflow_rp2040_watchdog_initialize(uint32_t timeout_microseconds) {
  /* Round up because the SDK accepts integer milliseconds. */
  watchdog_enable((timeout_microseconds + 999U) / 1000U, true);
}

/* Refresh the watchdog after a completed supervisor pass proves forward progress. */
void microflow_rp2040_watchdog_service(void) { watchdog_update(); }

/* Force a controlled chip reset through the watchdog and never return. */
void microflow_rp2040_reboot(void) {
  watchdog_reboot(0U, 0U, 0U);
  for (;;) { tight_loop_contents(); }
}

/* Configure UART0 pins, framing, and receive IRQs for the upload transport. */
void microflow_rp2040_uart0_initialize(uint32_t baud_rate) {
  /* Configure 8-N-1 and enable RX interrupts; TX starts demand-disabled. */
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

/* Tell the ISR whether reading UART0 can complete without blocking. */
bool microflow_rp2040_uart0_read_ready(void) { return uart_is_readable(MICROFLOW_UART); }

/* Remove one known-available UART byte for transfer into the RX software ring. */
uint8_t microflow_rp2040_uart0_read(void) { return (uint8_t)uart_getc(MICROFLOW_UART); }

/* Tell the ISR whether UART0 can accept a byte without blocking. */
bool microflow_rp2040_uart0_write_ready(void) { return uart_is_writable(MICROFLOW_UART); }

/* Place one known-writable byte into UART0 without text translation. */
void microflow_rp2040_uart0_write(uint8_t value) { uart_putc_raw(MICROFLOW_UART, (char)value); }

/* Enable TX IRQs only while software has queued bytes, avoiding interrupt storms. */
void microflow_rp2040_uart0_set_transmit_interrupt(bool enabled) {
  /* RX remains enabled while TX follows whether queued output exists. */
  uart_transmit_interrupt_enabled = enabled;
  uart_set_irq_enables(MICROFLOW_UART, true, uart_transmit_interrupt_enabled);
}

/* Save and disable interrupts for a short board-owned shared-state operation. */
uint32_t microflow_rp2040_interrupt_lock(void) { return save_and_disable_interrupts(); }

/* Restore the caller's prior interrupt state rather than blindly enabling IRQs. */
void microflow_rp2040_interrupt_unlock(uint32_t state) { restore_interrupts(state); }
