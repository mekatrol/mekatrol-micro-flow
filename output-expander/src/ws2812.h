#ifndef OUTPUT_EXPANDER_WS2812_H
#define OUTPUT_EXPANDER_WS2812_H

#include "status_led.h"

#include <stdbool.h>
#include <stdint.h>

#define WS2812_BIT_COUNT 24U
#define WS2812_TAIL_COUNT 16U
#define WS2812_DUTY_ZERO 18U
#define WS2812_DUTY_ONE 36U
#define WS2812_RESET_TICKS UINT32_C(14400)

typedef enum {
  WS2812_IDLE,
  WS2812_TRANSMITTING,
  WS2812_RESET_LOW
} ws2812_state_t;

typedef struct {
  /* A DMA-owned LOW tail keeps transfer-complete well beyond the final data
   * bit, independent of the exact CCR preload/update ordering in TIM1. */
  uint16_t duty[WS2812_BIT_COUNT];
  uint16_t trailing_zero[WS2812_TAIL_COUNT];
} ws2812_waveform_t;

typedef struct {
  ws2812_waveform_t waveform;
  rgb_t displayed;
  rgb_t pending;
  uint32_t reset_started;
  ws2812_state_t state;
  bool displayed_valid;
  bool pending_valid;
} ws2812_t;

void ws2812_init(ws2812_t *driver);
void ws2812_set_color(ws2812_t *driver, rgb_t color);
void ws2812_service(ws2812_t *driver, uint32_t now_ticks);
ws2812_state_t ws2812_state(const ws2812_t *driver);

/* Board backend. These operations are non-blocking. */
void ws2812_hw_init(void);
void ws2812_hw_start(const ws2812_waveform_t *waveform);
bool ws2812_hw_complete(void);
void ws2812_hw_force_low(void);

#endif
