#ifndef MICROFLOW_SOC_RP2040_H
#define MICROFLOW_SOC_RP2040_H

#include <stdbool.h>
#include <stdint.h>

void microflow_rp2040_initialize(void);
uint32_t microflow_rp2040_monotonic_microseconds(void);
void microflow_rp2040_gpio_output_initialize(uint32_t pin);
void microflow_rp2040_gpio_write(uint32_t pin, bool high);
void microflow_rp2040_ws2812_initialize(uint32_t pin);
void microflow_rp2040_ws2812_write(uint32_t pin, uint8_t red, uint8_t green, uint8_t blue);
void microflow_rp2040_delay_ms(uint32_t milliseconds);

#endif
