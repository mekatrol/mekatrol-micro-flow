#include <microflow/internal/board.h>

#include "esp32.h"

#define WAVESHARE_ESP32_S3_ZERO_STATUS_LED_PIN 21U

void microflow_board_initialize(void) {
  microflow_esp32_initialize();
  microflow_esp32_ws2812_initialize(WAVESHARE_ESP32_S3_ZERO_STATUS_LED_PIN);
}

void microflow_board_status_rgb(uint8_t red, uint8_t green, uint8_t blue) {
  microflow_esp32_ws2812_write(WAVESHARE_ESP32_S3_ZERO_STATUS_LED_PIN, red, green, blue);
}

void microflow_board_delay_ms(uint32_t milliseconds) {
  microflow_esp32_delay_ms(milliseconds);
}
