/* Pico SDK-backed RP2040 adapter used by the maintained Pico board build. */
#ifndef MICROFLOW_SOC_RP2040_SDK_H
#define MICROFLOW_SOC_RP2040_SDK_H

#include <stdbool.h>
#include <stdint.h>

/* Expose initialization without leaking SDK types into board code. */
void microflow_rp2040_initialize(void);
/* GPIO primitives intentionally use project-owned scalar types only. */
void microflow_rp2040_gpio_output_initialize(uint32_t pin);
void microflow_rp2040_gpio_write(uint32_t pin, bool high);
/* Hide status-pixel PIO lifecycle here so the board layer requests only RGB state. */
void microflow_rp2040_ws2812_initialize(uint32_t pin);
void microflow_rp2040_ws2812_write(uint32_t pin, uint8_t red, uint8_t green,
                                   uint8_t blue);
void microflow_rp2040_delay_ms(uint32_t milliseconds);
/* Requests a platform reset; this operation is intentionally non-returning. */
void microflow_rp2040_reboot(void);

/* Read the current RP2040 system clock frequency in hertz. */
uint32_t microflow_rp2040_system_clock_hz(void);

#endif
