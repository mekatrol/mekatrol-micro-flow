/* Pico SDK-backed RP2040 adapter used by the maintained Pico board build. */
#ifndef MICROFLOW_SOC_RP2040_SDK_H
#define MICROFLOW_SOC_RP2040_SDK_H

#include <stdbool.h>
#include <stdint.h>

/* Initialization and monotonic time primitives required by board startup. */
void microflow_rp2040_initialize(void);
uint32_t microflow_rp2040_monotonic_microseconds(void);
/* GPIO primitives intentionally use project-owned scalar types only. */
void microflow_rp2040_gpio_output_initialize(uint32_t pin);
void microflow_rp2040_gpio_input_initialize(uint32_t pin);
bool microflow_rp2040_gpio_read(uint32_t pin);
void microflow_rp2040_gpio_write(uint32_t pin, bool high);
/* Status-pixel support hides PIO/SDK lifecycle from the board layer. */
void microflow_rp2040_ws2812_initialize(uint32_t pin);
void microflow_rp2040_ws2812_write(uint32_t pin, uint8_t red, uint8_t green,
                                   uint8_t blue);
void microflow_rp2040_delay_ms(uint32_t milliseconds);
/* Supervisor platform primitives for idle waiting and watchdog servicing. */
void microflow_rp2040_wait_until(const volatile uint32_t *wake_events,
                                 bool has_deadline, uint32_t deadline);
void microflow_rp2040_watchdog_initialize(uint32_t timeout_microseconds);
void microflow_rp2040_watchdog_service(void);
/* Requests a platform reset; this operation is intentionally non-returning. */
void microflow_rp2040_reboot(void);
/* Non-blocking UART0 access used by bounded supervisor services. */
void microflow_rp2040_uart0_initialize(uint32_t baud_rate);
bool microflow_rp2040_uart0_read_ready(void);
uint8_t microflow_rp2040_uart0_read(void);
bool microflow_rp2040_uart0_write_ready(void);
void microflow_rp2040_uart0_write(uint8_t value);
void microflow_rp2040_uart0_set_transmit_interrupt(bool enabled);
/* Save/restore interrupt state brackets short shared-state snapshots. */
uint32_t microflow_rp2040_interrupt_lock(void);
void microflow_rp2040_interrupt_unlock(uint32_t state);

/* Board-defined event hooks called by SDK interrupt adapters. */
void microflow_rp2040_timer_alarm0_event(void);
void microflow_rp2040_uart0_event(void);
void microflow_rp2040_usb_event(void);

#endif
