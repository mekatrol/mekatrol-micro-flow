#ifndef MICROFLOW_SOC_RP2040_H
#define MICROFLOW_SOC_RP2040_H

#include <stdint.h>

void microflow_rp2040_initialize(void);
void microflow_rp2040_ws2812_initialize(uint32_t pin);
void microflow_rp2040_ws2812_write(uint32_t pin, uint8_t red, uint8_t green, uint8_t blue);
void microflow_rp2040_delay_ms(uint32_t milliseconds);

#endif
