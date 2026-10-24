#include <microflow/internal/board.h>

#include "rp2040_sdk.h"

#include <stdbool.h>

#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
#define STATUS_LED_PIN 16U
#else
#define RASPBERRY_PI_PICO_STATUS_LED_PIN 25U
#define STATUS_LED_PIN RASPBERRY_PI_PICO_STATUS_LED_PIN
#endif

void microflow_board_initialize(void) {
  microflow_rp2040_initialize();
#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
  microflow_rp2040_ws2812_initialize(STATUS_LED_PIN);
#else
  microflow_rp2040_gpio_output_initialize(STATUS_LED_PIN);
#endif
}

uint32_t microflow_board_monotonic_ticks(void) {
  return microflow_rp2040_monotonic_microseconds();
}

void microflow_board_status_rgb(uint8_t red, uint8_t green, uint8_t blue) {
#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
  microflow_rp2040_ws2812_write(STATUS_LED_PIN, red, green, blue);
#else
  const bool enabled = (red != 0U) || (green != 0U) || (blue != 0U);
  microflow_rp2040_gpio_write(STATUS_LED_PIN, enabled);
#endif
}

void microflow_board_delay_ms(uint32_t milliseconds) {
  microflow_rp2040_delay_ms(milliseconds);
}
