#ifndef OUTPUT_EXPANDER_STATUS_LED_H
#define OUTPUT_EXPANDER_STATUS_LED_H

#include <stdbool.h>
#include <stdint.h>

/* Human-visible status policy shared by main and the pure policy tests. */
#define STATUS_LED_BRIGHTNESS 16U
#define STATUS_LED_COMMAND_BRIGHTNESS 64U
#define STATUS_LED_COMMAND_DURATION_MS UINT32_C(300)

typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} rgb_t;

typedef enum {
  STATUS_LED_UNCONFIGURED,
  STATUS_LED_HEALTHY,
  STATUS_LED_COMMAND,
  STATUS_LED_COMMS_TIMEOUT,
  STATUS_LED_STORAGE_FAULT
} status_led_status_t;

rgb_t status_led_color(status_led_status_t status, uint8_t brightness);
rgb_t status_led_pattern(uint32_t now_ms, uint8_t brightness,
                         bool error_present, bool command_active,
                         uint32_t command_elapsed_ms);
uint32_t status_led_grb_word(rgb_t color);
bool rgb_equal(rgb_t left, rgb_t right);

#endif
