#include "status_led.h"

#include <assert.h>

int main(void) {
  assert(status_led_grb_word((rgb_t){0x12U, 0x34U, 0x56U}) ==
         UINT32_C(0x341256));
  assert(rgb_equal(status_led_pattern(0U, 32U, false, false, 0U),
                   (rgb_t){0U, 32U, 0U}));
  assert(rgb_equal(status_led_pattern(20U, 32U, false, false, 0U),
                   (rgb_t){0U, 0U, 0U}));
  assert(rgb_equal(status_led_pattern(500U, 32U, false, true, 10U),
                   (rgb_t){0U, 0U, STATUS_LED_COMMAND_BRIGHTNESS}));
  assert(rgb_equal(status_led_pattern(0U, 32U, true, true, 10U),
                   (rgb_t){0U, 0U, STATUS_LED_COMMAND_BRIGHTNESS}));
  assert(rgb_equal(status_led_pattern(300U, 32U, true, true,
                                      STATUS_LED_COMMAND_DURATION_MS),
                   (rgb_t){0U, 0U, 0U}));
  assert(rgb_equal(status_led_pattern(299U, 32U, false, true,
                                      STATUS_LED_COMMAND_DURATION_MS - 1U),
                   (rgb_t){0U, 0U, STATUS_LED_COMMAND_BRIGHTNESS}));
  assert(rgb_equal(status_led_pattern(600U, 32U, true, false, 0U),
                   (rgb_t){32U, 0U, 0U}));
  assert(rgb_equal(status_led_color(STATUS_LED_UNCONFIGURED, 30U),
                   (rgb_t){30U, 10U, 0U}));
  return 0;
}
