#include "ws2812.h"

#include <stddef.h>

_Static_assert(offsetof(ws2812_waveform_t, trailing_zero) ==
                   sizeof(uint16_t) * WS2812_BIT_COUNT,
               "DMA trailing zero must follow the duty array");

static void encode(rgb_t color, uint16_t duty[WS2812_BIT_COUNT]) {
  const uint32_t word = status_led_grb_word(color);
  for (uint32_t bit = 0U; bit < WS2812_BIT_COUNT; ++bit) {
    const uint32_t mask = UINT32_C(1) << (23U - bit);
    duty[bit] = ((word & mask) != 0U) ? WS2812_DUTY_ONE : WS2812_DUTY_ZERO;
  }
}

static void start_pending(ws2812_t *driver) {
  const rgb_t color = driver->pending;
  driver->pending_valid = false;
  encode(color, driver->waveform.duty);
  driver->waveform.trailing_zero[0] = 0U;
  driver->waveform.trailing_zero[1] = 0U;
  driver->displayed = color;
  driver->displayed_valid = true;
  ws2812_hw_start(&driver->waveform);
  driver->state = WS2812_TRANSMITTING;
}

void ws2812_init(ws2812_t *driver) {
  driver->displayed = (rgb_t){0U, 0U, 0U};
  driver->pending = driver->displayed;
  driver->reset_started = 0U;
  driver->state = WS2812_IDLE;
  driver->displayed_valid = false;
  driver->pending_valid = false;
  driver->waveform.trailing_zero[0] = 0U;
  driver->waveform.trailing_zero[1] = 0U;
  ws2812_hw_init();
}

void ws2812_set_color(ws2812_t *driver, rgb_t color) {
  if (driver->pending_valid && rgb_equal(driver->pending, color)) {
    return;
  }
  if (!driver->pending_valid && driver->displayed_valid &&
      rgb_equal(driver->displayed, color)) {
    return;
  }
  driver->pending = color;
  driver->pending_valid = true;
}

void ws2812_service(ws2812_t *driver, uint32_t now_ticks) {
  if (driver->state == WS2812_TRANSMITTING && ws2812_hw_complete()) {
    ws2812_hw_force_low();
    driver->reset_started = now_ticks;
    driver->state = WS2812_RESET_LOW;
  }
  if (driver->state == WS2812_RESET_LOW &&
      (uint32_t)(now_ticks - driver->reset_started) >= WS2812_RESET_TICKS) {
    driver->state = WS2812_IDLE;
  }
  if (driver->state == WS2812_IDLE && driver->pending_valid) {
    start_pending(driver);
  }
}

ws2812_state_t ws2812_state(const ws2812_t *driver) { return driver->state; }
