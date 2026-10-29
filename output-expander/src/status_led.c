#include "status_led.h"

static const rgb_t RGB_OFF = {0U, 0U, 0U};

bool rgb_equal(rgb_t left, rgb_t right) {
  return left.red == right.red && left.green == right.green &&
         left.blue == right.blue;
}

uint32_t status_led_grb_word(rgb_t color) {
  return ((uint32_t)color.green << 16) | ((uint32_t)color.red << 8) |
         (uint32_t)color.blue;
}

rgb_t status_led_color(status_led_status_t status, uint8_t brightness) {
  switch (status) {
  case STATUS_LED_UNCONFIGURED:
    return (rgb_t){brightness, (uint8_t)(brightness / 3U), 0U};
  case STATUS_LED_HEALTHY:
    return (rgb_t){0U, brightness, 0U};
  case STATUS_LED_COMMAND:
    return (rgb_t){0U, 0U, brightness};
  case STATUS_LED_COMMS_TIMEOUT:
  case STATUS_LED_STORAGE_FAULT:
    return (rgb_t){brightness, 0U, 0U};
  }
  return RGB_OFF;
}

rgb_t status_led_pattern(uint32_t now_ms, uint8_t brightness,
                         bool error_present, bool command_active,
                         bool discovery_pending,
                         uint32_t command_elapsed_ms) {
  /*
   * Discovery owns the visible indication only while this device still owes
   * the master a reply. Alternate equal blue and OFF phases to make the scan
   * unmistakably different from the solid 200 ms command acknowledgement.
   * As soon as communications clears the pending flag after response, cancel,
   * or timeout, this branch is skipped and normal green/error policy resumes.
   */
  if (discovery_pending) {
    return (((now_ms / STATUS_LED_DISCOVERY_FLASH_MS) & UINT32_C(1)) == 0U)
               ? (rgb_t){0U, 0U, STATUS_LED_COMMAND_BRIGHTNESS}
               : RGB_OFF;
  }

  if (command_active &&
      command_elapsed_ms < STATUS_LED_COMMAND_DURATION_MS) {
    return (rgb_t){0U, 0U, STATUS_LED_COMMAND_BRIGHTNESS};
  }
  if (error_present) {
    return (((now_ms / UINT32_C(300)) & UINT32_C(1)) == 0U)
               ? (rgb_t){brightness, 0U, 0U}
               : RGB_OFF;
  }
  return (now_ms % UINT32_C(1000) < UINT32_C(20)) ? (rgb_t){0U, brightness, 0U}
                                                  : RGB_OFF;
}
