#include "output_driver.h"

#include "hal.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * All sequencing state is foreground-owned. Keeping the requested and applied
 * masks separate makes the physical inrush-current rule independent of how
 * protocol commands are grouped or timed.
 */
static uint8_t requested_mask;
static uint8_t applied_mask;
static uint8_t next_channel;
static uint8_t activation_delay_units;
static uint32_t last_activation_ticks;
static bool activation_time_valid;

/* SysTick runs from the fixed 48 MHz HCLK configured by hal_init(). */
#define OUTPUT_DRIVER_TICKS_PER_MS UINT32_C(48000)

static uint8_t select_next_output(uint8_t pending) {
  /*
   * Select exactly one pending bit. Round-robin selection prevents frequently
   * changed low-numbered channels from indefinitely starving higher channels.
   * Eight iterations is a small, deterministic upper bound.
   */
  for (uint8_t offset = 0U; offset < OUTPUT_DRIVER_CHANNEL_COUNT; ++offset) {
    const uint8_t channel =
        (uint8_t)((next_channel + offset) % OUTPUT_DRIVER_CHANNEL_COUNT);
    const uint8_t selected = (uint8_t)(UINT8_C(1) << channel);

    if ((pending & selected) != 0U) {
      next_channel = (uint8_t)((channel + 1U) % OUTPUT_DRIVER_CHANNEL_COUNT);
      return selected;
    }
  }

  return 0U;
}

void output_driver_init(void) {
  /*
   * hal_init() has already established the physical all-OFF state. Reset both
   * masks to match it and establish the safe default delay. No GPIO
   * write is needed, avoiding any possibility of a startup pulse.
   */
  requested_mask = 0U;
  applied_mask = 0U;
  next_channel = 0U;
  activation_delay_units = OUTPUT_DRIVER_DEFAULT_ACTIVATION_DELAY_UNITS;
  last_activation_ticks = 0U;
  activation_time_valid = false;
}

uint8_t output_driver_request(uint8_t new_requested_mask) {
  /*
   * Replacing rather than queueing the target lets newer commands cancel
   * obsolete pending activations during a multi-second eight-channel ramp.
   */
  requested_mask = new_requested_mask;

  /*
   * OFF transitions are safe and must not wait. Applying them here means an
   * acknowledged command has already removed every unwanted load. Only the
   * service function below is permitted to create ON transitions.
   */
  const uint8_t turn_off =
      (uint8_t)(applied_mask & (uint8_t)~requested_mask);

  if (turn_off != 0U) {
    hal_output_off(turn_off);
    applied_mask = (uint8_t)(applied_mask & (uint8_t)~turn_off);
  }

  return requested_mask;
}

void output_driver_service(uint32_t now_ticks) {
  /*
   * Repeat the immediate-OFF calculation defensively. Normal requests already
   * perform it, but keeping service self-contained protects future internal
   * policies that may change the requested mask.
   */
  const uint8_t turn_off =
      (uint8_t)(applied_mask & (uint8_t)~requested_mask);

  if (turn_off != 0U) {
    hal_output_off(turn_off);
    applied_mask = (uint8_t)(applied_mask & (uint8_t)~turn_off);
  }

  const uint8_t pending =
      (uint8_t)(requested_mask & (uint8_t)~applied_mask);

  if (pending == 0U) {
    return;
  }

  /*
   * Unsigned subtraction handles one SysTick wrap. The maximum supported delay
   * is far shorter than the roughly 89-second wrap, so elapsed time remains
   * unambiguous.
   */
  /*
   * Keep the stored configuration in compact 100 ms units and expand it only
   * for the runtime tick comparison. The maximum calculation is 100 * 100 *
   * 48000 = 480,000,000 ticks, which safely fits in uint32_t.
   */
  const uint32_t required_ticks =
      (uint32_t)activation_delay_units * OUTPUT_DRIVER_DELAY_UNIT_MS *
      OUTPUT_DRIVER_TICKS_PER_MS;

  if (activation_time_valid &&
      (uint32_t)(now_ticks - last_activation_ticks) < required_ticks) {
    return;
  }

  const uint8_t selected = select_next_output(pending);

  if (selected == 0U) {
    return;
  }

  /*
   * selected contains exactly one bit. Locating the only ON HAL call here is
   * the hard guarantee that simultaneous activation edges cannot be generated.
   */
  hal_output_on(selected);
  applied_mask = (uint8_t)(applied_mask | selected);
  last_activation_ticks = now_ticks;
  activation_time_valid = true;
}

uint8_t output_driver_get(void) { return applied_mask; }

uint8_t output_driver_get_requested(void) { return requested_mask; }

uint8_t output_driver_get_activation_delay_units(void) {
  return activation_delay_units;
}

bool output_driver_set_activation_delay_units(uint8_t delay_units) {
  /*
   * This lower bound is a safety constraint. Reject an unsafe value so the
   * controller sees its error instead of silently weakening or clamping it.
   */
  if (delay_units < OUTPUT_DRIVER_MIN_ACTIVATION_DELAY_UNITS ||
      delay_units > OUTPUT_DRIVER_MAX_ACTIVATION_DELAY_UNITS) {
    return false;
  }

  activation_delay_units = delay_units;
  return true;
}
