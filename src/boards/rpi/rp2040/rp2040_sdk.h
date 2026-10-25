/* Pico SDK-backed RP2040 adapter used by the maintained Pico board build. */
#ifndef MICROFLOW_SOC_RP2040_SDK_H
#define MICROFLOW_SOC_RP2040_SDK_H

#include <stdbool.h>
#include <stdint.h>

/* Expose initialization and time primitives so board startup is independent of SDK types. */
void     microflow_rp2040_initialize(void);
uint32_t microflow_rp2040_monotonic_microseconds(void);
/* GPIO primitives intentionally use project-owned scalar types only. */
void microflow_rp2040_gpio_output_initialize(uint32_t pin);
void microflow_rp2040_gpio_input_initialize(uint32_t pin);
bool microflow_rp2040_gpio_read(uint32_t pin);
void microflow_rp2040_gpio_write(uint32_t pin, bool high);
/* Hide status-pixel PIO lifecycle here so the board layer requests only RGB state. */
void microflow_rp2040_ws2812_initialize(uint32_t pin);
void microflow_rp2040_ws2812_write(uint32_t pin, uint8_t red, uint8_t green,
                                   uint8_t blue);
void microflow_rp2040_delay_ms(uint32_t milliseconds);
void microflow_rp2040_watchdog_initialize(uint32_t timeout_microseconds);
void microflow_rp2040_watchdog_service(void);
/* Requests a platform reset; this operation is intentionally non-returning. */
void microflow_rp2040_reboot(void);
/* Non-blocking UART0 access used by bounded supervisor services. */
void    microflow_rp2040_uart0_initialize(uint32_t baud_rate);
bool    microflow_rp2040_uart0_read_ready(void);
uint8_t microflow_rp2040_uart0_read(void);
bool    microflow_rp2040_uart0_write_ready(void);
void    microflow_rp2040_uart0_write(uint8_t value);
void    microflow_rp2040_uart0_set_transmit_interrupt(bool enabled);
/* Save and restore PRIMASK so a critical section does not enable IRQs disabled by its caller. */
uint32_t microflow_rp2040_interrupt_lock(void);
void     microflow_rp2040_interrupt_unlock(uint32_t state);

/* Declare board hooks so SDK interrupt adapters can signal work without owning application state. */
void microflow_rp2040_timer_alarm0_event(void);
void microflow_rp2040_uart0_event(void);
void microflow_rp2040_usb_event(void);

/* Read the current RP2040 system clock frequency in hertz. */
uint32_t microflow_rp2040_system_clock_hz(void);

#endif
