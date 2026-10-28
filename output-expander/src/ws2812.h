#ifndef OUTPUT_EXPANDER_WS2812_H
#define OUTPUT_EXPANDER_WS2812_H

#include "status_led.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * One WS2812 colour frame contains three eight-bit channels. The device's wire
 * order is green, red, blue (GRB), but the number of bits is still:
 *
 *     3 colour channels * 8 bits per channel = 24 serial data bits.
 *
 * Each entry in duty[] describes one of those bits. Keeping this as an explicit
 * protocol constant also makes array bounds and DMA transfer arithmetic agree.
 */
#define WS2812_BIT_COUNT 24U

/*
 * Append sixteen zero-duty PWM periods after the 24 data periods. At the
 * 800 kHz WS2812 bit rate, one period is 1 / 800,000 = 1.25 us, therefore:
 *
 *     16 periods * 1.25 us = 20 us of guaranteed hardware-generated LOW.
 *
 * This is not the LED's latch/reset interval; the separate 300 us reset state
 * follows it. The tail is a guard against TIM1 CCR2's active/preload pipeline:
 * DMA cannot report completion near the final blue bit because it must first
 * transfer all sixteen zero compare values. Sixteen is deliberately much more
 * than the one active plus one preload stage that needs draining, while costing
 * only 16 * sizeof(uint16_t) = 32 bytes in the persistent driver object.
 */
#define WS2812_TAIL_COUNT 16U

/*
 * TIM1 runs at 48 MHz, so one timer count lasts:
 *
 *     1 / 48,000,000 s = 20.833 ns.
 *
 * A complete WS2812 bit lasts 60 counts, or 60 * 20.833 ns = 1.25 us. For a
 * logical zero, CCR2=18 keeps PWM mode 1 HIGH while CNT is 0..17:
 *
 *     18 / 48,000,000 s = 375 ns HIGH,
 *     (60 - 18) / 48,000,000 s = 875 ns LOW.
 *
 * These times sit within the timing window of the selected WS2812 protocol.
 */
#define WS2812_DUTY_ZERO 18U

/*
 * For a logical one, CCR2=36 keeps PWM mode 1 HIGH for 36 timer counts:
 *
 *     36 / 48,000,000 s = 750 ns HIGH,
 *     (60 - 36) / 48,000,000 s = 500 ns LOW.
 *
 * The total remains 60 counts/1.25 us; only the HIGH-to-LOW ratio changes.
 */
#define WS2812_DUTY_ONE 36U

/*
 * SysTick also runs directly from the 48 MHz system clock, giving 48 ticks per
 * microsecond. Hold the output LOW for a conservative 300 us latch interval:
 *
 *     48 ticks/us * 300 us = 14,400 ticks.
 *
 * A LOW interval tells the WS2812 that the preceding 24 bits form a complete
 * frame and causes it to transfer the received GRB value to its LED drivers.
 */
#define WS2812_RESET_TICKS UINT32_C(14400)

/* The foreground state machine prevents waveform memory from being modified
 * while DMA owns it and guarantees a latch interval between successive frames.
 */
typedef enum {
  /* No timer/DMA transfer is active and the required reset LOW interval has
   * completed. A pending colour may safely be encoded and started. */
  WS2812_IDLE,

  /* TIM1 is generating PWM and DMA1 channel 3 is feeding CCR2. The waveform
   * buffer is immutable in this state because DMA is reading from it. */
  WS2812_TRANSMITTING,

  /* TIM1 and DMA are stopped, PC7 is forced LOW, and reset_started records when
   * the 300 us latch interval began. A new frame must not start yet. */
  WS2812_RESET_LOW
} ws2812_state_t;

typedef struct {
  /* One 16-bit TIM1 CCR2 compare value per GRB data bit. Sixteen bits are used
   * because CCR2 and DMA are configured for halfword accesses. */
  uint16_t duty[WS2812_BIT_COUNT];

  /* Zero CCR2 values stored immediately after duty[]. DMA walks into this array
   * to keep PC7 LOW before transfer-complete can become true. */
  uint16_t trailing_zero[WS2812_TAIL_COUNT];
} ws2812_waveform_t;

typedef struct {
  /* Persistent DMA source storage. It must outlive every asynchronous transfer.
   */
  ws2812_waveform_t waveform;

  /* Colour represented by the current/most recently completed frame. */
  rgb_t displayed;

  /* Latest requested colour waiting for the driver to become idle. */
  rgb_t pending;

  /* 48 MHz SysTick timestamp captured when PC7 was forced LOW. */
  uint32_t reset_started;

  /* Current ownership/timing phase described by ws2812_state_t. */
  ws2812_state_t state;

  /* False until at least one colour has been selected for transmission. */
  bool displayed_valid;

  /* True when pending contains a request not yet encoded into waveform. */
  bool pending_valid;
} ws2812_t;

/* Initialise software state and configure PC7, TIM1, and DMA1 channel 3. */
void ws2812_init(ws2812_t *driver);

/* Request a colour without blocking. During a transfer, the newest request
 * replaces any older pending request but never modifies DMA-owned memory. */
void ws2812_set_color(ws2812_t *driver, rgb_t color);

/* Advance transfer-complete and reset timing state using the 48 MHz timestamp.
 */
void ws2812_service(ws2812_t *driver, uint32_t now_ticks);

/* Return the current state for diagnostics and tests. */
ws2812_state_t ws2812_state(const ws2812_t *driver);

/* Configure the CH32V003 peripherals without starting a frame. */
void ws2812_hw_init(void);

/* Stage the first two duties, configure DMA for the remainder, and start TIM1.
 */
void ws2812_hw_start(const ws2812_waveform_t *waveform);

/* Poll DMA1 channel 3's sticky transfer-complete flag. */
bool ws2812_hw_complete(void);

/* Stop requests/DMA/TIM1, disconnect CH2, and establish a definite LOW on PC7.
 */
void ws2812_hw_force_low(void);

#endif
