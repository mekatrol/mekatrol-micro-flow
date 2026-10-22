#ifndef MICROFLOW_SOC_ESP32_H
#define MICROFLOW_SOC_ESP32_H

#include <stdint.h>

void microflow_esp32_initialize(void);
void microflow_esp32_ws2812_initialize(uint32_t pin);
void microflow_esp32_ws2812_write(uint32_t pin, uint8_t red, uint8_t green, uint8_t blue);
void microflow_esp32_delay_ms(uint32_t milliseconds);

#endif
