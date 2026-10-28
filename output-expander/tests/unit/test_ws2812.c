#include "ws2812.h"

#include <assert.h>
#include <stddef.h>

static bool complete;
static unsigned starts;
static unsigned forced_low;
static uint16_t captured[WS2812_BIT_COUNT + WS2812_TAIL_COUNT];

void ws2812_hw_init(void) {
  complete = false;
  starts = 0U;
  forced_low = 0U;
}

void ws2812_hw_start(const ws2812_waveform_t *waveform) {
  for (size_t i = 0U; i < WS2812_BIT_COUNT + WS2812_TAIL_COUNT; ++i) {
    captured[i] = (i < WS2812_BIT_COUNT)
                      ? waveform->duty[i]
                      : waveform->trailing_zero[i - WS2812_BIT_COUNT];
  }
  complete = false;
  starts++;
}

bool ws2812_hw_complete(void) { return complete; }
void ws2812_hw_force_low(void) { forced_low++; }

int main(void) {
  ws2812_t driver;
  ws2812_init(&driver);
  assert(ws2812_state(&driver) == WS2812_IDLE);

  /* GRB 0x80,0x40,0x01 proves byte order and MSB-first bit order. */
  ws2812_set_color(&driver, (rgb_t){0x40U, 0x80U, 0x01U});
  ws2812_service(&driver, 10U);
  assert(starts == 1U);
  assert(captured[0] == WS2812_DUTY_ONE);
  assert(captured[1] == WS2812_DUTY_ZERO);
  assert(captured[8] == WS2812_DUTY_ZERO);
  assert(captured[9] == WS2812_DUTY_ONE);
  assert(captured[23] == WS2812_DUTY_ONE);
  for (size_t i = WS2812_BIT_COUNT;
       i < WS2812_BIT_COUNT + WS2812_TAIL_COUNT; ++i) {
    assert(captured[i] == 0U);
  }

  /* Requests in flight collapse to the latest value without touching DMA. */
  ws2812_set_color(&driver, (rgb_t){1U, 2U, 3U});
  ws2812_set_color(&driver, (rgb_t){4U, 5U, 6U});
  assert(starts == 1U);
  complete = true;
  ws2812_service(&driver, 100U);
  assert(forced_low == 1U);
  assert(ws2812_state(&driver) == WS2812_RESET_LOW);
  const uint32_t first_reset = 100U;
  ws2812_service(&driver, first_reset + WS2812_RESET_TICKS - 1U);
  assert(starts == 1U);
  ws2812_service(&driver, first_reset + WS2812_RESET_TICKS);
  assert(starts == 2U);
  assert(ws2812_state(&driver) == WS2812_TRANSMITTING);

  /* Re-requesting the currently displayed color produces no third frame. */
  complete = true;
  ws2812_service(&driver, 20000U);
  ws2812_service(&driver, 20000U + WS2812_RESET_TICKS);
  ws2812_set_color(&driver, (rgb_t){4U, 5U, 6U});
  ws2812_service(&driver, 40000U);
  assert(starts == 2U);
  return 0;
}
