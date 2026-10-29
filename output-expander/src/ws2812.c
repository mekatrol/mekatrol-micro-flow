#include "ws2812.h"

#include <stddef.h>

/*
 * WS2812 foreground driver
 * ------------------------
 *
 * A WS2812 accepts one colour as a serial stream of 24 bits. Each bit occupies
 * the same 1.25 us period, but a zero and a one use different HIGH times. The
 * hardware-specific module converts the duty values prepared here into those
 * pulses with TIM1 and DMA. This module deliberately contains no peripheral
 * register accesses; it owns colour encoding, buffer lifetime, and the
 * non-blocking transfer/reset state machine.
 *
 * The byte order on the wire is GRB, not the RGB order used by rgb_t. Within
 * every byte, the most-significant bit is transmitted first. After all 24 data
 * bits, the data line must remain LOW long enough for the LED to latch the new
 * colour. WS2812_RESET_TICKS is 300 us in the 48 MHz SysTick time base, which
 * is deliberately conservative for WS2812-compatible parts.
 *
 * The waveform buffer is a member of ws2812_t rather than a local array. DMA
 * reads the buffer after ws2812_hw_start() has returned, so stack-local storage
 * would become invalid while the transfer was still running. The application
 * keeps ws2812_t alive for the entire firmware lifetime.
 */

/*
 * The hardware starts DMA at duty[0] and then walks linearly through the rest
 * of ws2812_waveform_t. This assertion proves that the compiler inserted no
 * padding between the 24 data duties and the zero-duty tail. Without this
 * guarantee DMA could transmit padding bytes as compare values.
 */
_Static_assert(offsetof(ws2812_waveform_t, trailing_zero) ==
                   sizeof(uint16_t) * WS2812_BIT_COUNT,
               "DMA trailing zero must follow the duty array");

/* Convert one RGB colour into the 24 timer compare values consumed by TIM1. */
static void encode(rgb_t color, uint16_t duty[WS2812_BIT_COUNT]) {
  /* status_led_grb_word() rearranges the normal RGB structure into the wire
   * representation 0xGGRRBB. Bit 23 is therefore green bit 7, bit 15 is red
   * bit 7, and bit 7 is blue bit 7. */
  const uint32_t word = status_led_grb_word(color);

  for (uint32_t bit = 0U; bit < WS2812_BIT_COUNT; ++bit) {
    /* WS2812_BIT_COUNT-1 is 24-1=23, the highest valid bit index in a 24-bit
     * word. Subtracting the loop index walks 23,22,...,0, making the entire GRB
     * word—and consequently every eight-bit channel—most-significant-bit first.
     */
    const uint32_t mask = UINT32_C(1) << ((WS2812_BIT_COUNT - 1U) - bit);

    /* TIM1 counts 60 clocks per 1.25 us period. A zero uses 18 HIGH clocks
     * (375 ns) and a one uses 36 HIGH clocks (750 ns). PWM mode 1 drives the
     * output HIGH while CNT < CCR2 and LOW for the remainder of the period. */
    duty[bit] = ((word & mask) != 0U) ? WS2812_DUTY_ONE : WS2812_DUTY_ZERO;
  }
}

/* Begin transmitting the most recently requested colour while the driver is
 * idle. The caller guarantees that pending_valid is true and no DMA transfer
 * owns waveform at this point. */
static void start_pending(ws2812_t *driver) {
  /* Copy the three-byte request before changing its validity flag. This keeps
   * the transition explicit and ensures the encoded buffer and displayed
   * bookkeeping always describe the same colour. */
  const rgb_t color = driver->pending;
  driver->pending_valid = false;

  /* Encoding is safe only while idle: DMA reads this same array during a
   * transfer, so rewriting it in WS2812_TRANSMITTING would corrupt the frame.
   */
  encode(color, driver->waveform.duty);

  /* "Displayed" means the colour selected for the frame now being sent. It
   * is recorded before starting hardware so repeated requests can be ignored
   * while this immutable waveform is owned by DMA. */
  driver->displayed = color;
  driver->displayed_valid = true;

  /* ws2812_hw_start() stages the first compare value behind a LOW guard period
   * and starts TIM1 and DMA. It returns immediately. The hardware stream
   * contains one LOW guard, 24 data periods, and 16 tail periods. */
  ws2812_hw_start(&driver->waveform);
  driver->state = WS2812_TRANSMITTING;
}

void ws2812_init(ws2812_t *driver) {
  /* Initialise every state field explicitly. displayed_valid remains false
   * because no frame has reached the LED yet, even though displayed is set to
   * a harmless all-zero value for deterministic debugger output. */
  driver->displayed = (rgb_t){0U, 0U, 0U};
  driver->pending = driver->displayed;
  driver->reset_started = 0U;
  driver->state = WS2812_IDLE;
  driver->displayed_valid = false;
  driver->pending_valid = false;

  /* The DMA tail consists entirely of CCR2=0 values. In PWM mode 1 that means
   * CNT is never less than CCR2, so PC7 remains LOW for each complete tail
   * period. Sixteen periods provide 20 us of hardware-controlled separation
   * between the final blue bit and DMA transfer-complete. Initialise this once
   * because start_pending() never modifies the tail. */
  for (uint32_t i = 0U; i < WS2812_TAIL_COUNT; ++i) {
    driver->waveform.trailing_zero[i] = 0U;
  }

  /* Configure clocks, PC7, TIM1, and DMA's peripheral relationship. No LED
   * frame starts until a later call to ws2812_service(). */
  ws2812_hw_init();
}

void ws2812_set_color(ws2812_t *driver, rgb_t color) {
  /* Repeating the already-pending request cannot change the eventual output. */
  if (driver->pending_valid && rgb_equal(driver->pending, color)) {
    return;
  }

  /* If no newer request is queued and this colour is already being displayed,
   * avoid an unnecessary 24-bit frame and reset interval. */
  if (!driver->pending_valid && driver->displayed_valid &&
      rgb_equal(driver->displayed, color)) {
    return;
  }

  /* Latest request wins. Crucially, this updates only three bytes of pending
   * state; it never touches the DMA-owned waveform during transmission. */
  driver->pending = color;
  driver->pending_valid = true;
}

void ws2812_service(ws2812_t *driver, uint32_t now_ticks) {
  /* DMA transfer-complete is intentionally delayed until it has written all
   * 24 DMA-fed data duties and the 16-entry LOW tail. At that point the
   * final colour bit is far behind the timer's active/preload pipeline and it
   * is safe to stop the peripheral without shortening the blue byte. */
  if (driver->state == WS2812_TRANSMITTING && ws2812_hw_complete()) {
    ws2812_hw_force_low();

    /* Start measuring the WS2812 latch/reset LOW interval only after the pin
     * has been explicitly disconnected from TIM1 and forced LOW. */
    driver->reset_started = now_ticks;
    driver->state = WS2812_RESET_LOW;
  }

  /* Unsigned subtraction makes this elapsed-time comparison work correctly
   * across the 32-bit SysTick counter wrap, provided the interval is less than
   * half the counter range (300 us is vastly smaller). */
  if (driver->state == WS2812_RESET_LOW &&
      (uint32_t)(now_ticks - driver->reset_started) >= WS2812_RESET_TICKS) {
    driver->state = WS2812_IDLE;
  }

  /* This is a separate if rather than else-if so a queued latest colour can
   * start in the same service call that completes the reset interval. */
  if (driver->state == WS2812_IDLE && driver->pending_valid) {
    start_pending(driver);
  }
}

/* Expose state for diagnostics and tests without exposing mutable internals. */
ws2812_state_t ws2812_state(const ws2812_t *driver) { return driver->state; }
