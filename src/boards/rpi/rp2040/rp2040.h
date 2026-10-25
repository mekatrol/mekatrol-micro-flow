/* Bare-metal RP2040 services exposed to board composition code. */
#ifndef MICROFLOW_SOC_RP2040_H
#define MICROFLOW_SOC_RP2040_H

#include <stdbool.h>
#include <stdint.h>

/* Establishes clocks, timer, interrupts, and safe peripheral defaults. */
void microflow_rp2040_initialize(void);
/* Returns the wrapping hardware timer's low 32 microsecond bits. */
uint32_t microflow_rp2040_monotonic_microseconds(void);
/* GPIO and WS2812 functions provide minimal board-output primitives. */
void microflow_rp2040_gpio_output_initialize(uint32_t pin);
void microflow_rp2040_gpio_write(uint32_t pin, bool high);
void microflow_rp2040_ws2812_initialize(uint32_t pin);
void microflow_rp2040_ws2812_write(uint32_t pin, uint8_t red, uint8_t green, uint8_t blue);
void microflow_rp2040_delay_ms(uint32_t milliseconds);
/* Wait/watchdog functions support the supervisor's idle and liveness policy. */
void microflow_rp2040_wait_until(
    const volatile uint32_t *wake_events, bool has_deadline, uint32_t deadline);
void microflow_rp2040_watchdog_initialize(uint32_t timeout_microseconds);
void microflow_rp2040_watchdog_service(void);
/* UART0 functions form the non-blocking interrupt-driven serial boundary. */
void microflow_rp2040_uart0_initialize(uint32_t baud_rate);
bool microflow_rp2040_uart0_read_ready(void);
uint8_t microflow_rp2040_uart0_read(void);
bool microflow_rp2040_uart0_write_ready(void);
void microflow_rp2040_uart0_write(uint8_t value);
void microflow_rp2040_uart0_set_transmit_interrupt(bool enabled);
/* Critical sections return prior interrupt state for exact restoration. */
uint32_t microflow_rp2040_interrupt_lock(void);
void microflow_rp2040_interrupt_unlock(uint32_t state);
/* Weak board hooks transfer SoC interrupt work without board dependencies. */
void microflow_rp2040_timer_alarm0_event(void);
void microflow_rp2040_uart0_event(void);
void microflow_rp2040_usb_event(void);
/* Vector-table handlers acknowledge hardware before invoking board hooks. */
void microflow_rp2040_timer0_irq_handler(void);
void microflow_rp2040_uart0_irq_handler(void);
void microflow_rp2040_usbctrl_irq_handler(void);

#endif
