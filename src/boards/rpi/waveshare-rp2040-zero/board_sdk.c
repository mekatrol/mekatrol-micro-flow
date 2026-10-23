#include <microflow/internal/board.h>

#include "rp2040_sdk.h"

#define WAVESHARE_RP2040_ZERO_STATUS_LED_PIN 16U

void microflow_board_initialize(void) {
  microflow_rp2040_initialize();
  microflow_rp2040_ws2812_initialize(WAVESHARE_RP2040_ZERO_STATUS_LED_PIN);
}

uint32_t microflow_board_monotonic_ticks(void) {
  return microflow_rp2040_monotonic_microseconds();
}

void microflow_board_status_rgb(uint8_t red, uint8_t green, uint8_t blue) {
  microflow_rp2040_ws2812_write(WAVESHARE_RP2040_ZERO_STATUS_LED_PIN, red, green, blue);
}

void microflow_board_delay_ms(uint32_t milliseconds) {
  microflow_rp2040_delay_ms(milliseconds);
}
