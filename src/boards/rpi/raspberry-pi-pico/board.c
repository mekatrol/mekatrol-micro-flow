#include <microflow/internal/board.h>

#include "rp2040.h"

#define RASPBERRY_PI_PICO_STATUS_LED_PIN 25U

void microflow_board_initialize(void) {
  microflow_rp2040_initialize();
  microflow_rp2040_gpio_output_initialize(RASPBERRY_PI_PICO_STATUS_LED_PIN);
}

void microflow_board_status_rgb(uint8_t red, uint8_t green, uint8_t blue) {
  const bool enabled = (red != 0U) || (green != 0U) || (blue != 0U);
  microflow_rp2040_gpio_write(RASPBERRY_PI_PICO_STATUS_LED_PIN, enabled);
}

void microflow_board_delay_ms(uint32_t milliseconds) {
  microflow_rp2040_delay_ms(milliseconds);
}
