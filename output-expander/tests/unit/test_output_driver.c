#include "hal.h"
#include "output_driver.h"

#include <assert.h>
#include <stdint.h>

#define TICKS_PER_MS UINT32_C(48000)

/* The fake HAL records physical transitions so timing and one-bit activation
 * invariants can be proved on the host without accessing MCU GPIO registers. */
static uint8_t hardware_mask;
static uint8_t last_on_argument;
static uint8_t last_off_argument;
static unsigned int on_calls;
static unsigned int off_calls;

void hal_output_on(uint8_t output_mask) {
  /* Production must never ask the HAL to activate zero or multiple loads. */
  assert(output_mask != 0U);
  assert((output_mask & (uint8_t)(output_mask - 1U)) == 0U);
  last_on_argument = output_mask;
  ++on_calls;
  hardware_mask = (uint8_t)(hardware_mask | output_mask);
}

void hal_output_off(uint8_t output_mask) {
  last_off_argument = output_mask;
  ++off_calls;
  hardware_mask = (uint8_t)(hardware_mask & (uint8_t)~output_mask);
}

static void reset_device(void) {
  hardware_mask = 0U;
  last_on_argument = 0U;
  last_off_argument = 0U;
  on_calls = 0U;
  off_calls = 0U;
  output_driver_init();
}

static void test_defaults_and_validation(void) {
  reset_device();
  assert(output_driver_get() == 0U);
  assert(output_driver_get_requested() == 0U);
  assert(output_driver_get_activation_delay_units() == 5U);
  assert(!output_driver_set_activation_delay_units(4U));
  assert(!output_driver_set_activation_delay_units(101U));
  assert(output_driver_get_activation_delay_units() == 5U);
  assert(output_driver_set_activation_delay_units(5U));
}

static void test_staggered_activation(void) {
  reset_device();
  assert(output_driver_set_activation_delay_units(5U));
  assert(output_driver_request(0xFFU) == 0xFFU);
  assert(output_driver_get() == 0U);

  /* The first pending output starts immediately; subsequent calls are gated. */
  output_driver_service(100U);
  assert(output_driver_get() == 0x01U);
  assert(last_on_argument == 0x01U);
  output_driver_service(100U + 500U * TICKS_PER_MS - 1U);
  assert(on_calls == 1U);
  output_driver_service(100U + 500U * TICKS_PER_MS);
  assert(output_driver_get() == 0x03U);
  assert(last_on_argument == 0x02U);

  /* A late call activates one output only; the sequencer never catches up. */
  output_driver_service(100U + 5000U * TICKS_PER_MS);
  assert(output_driver_get() == 0x07U);
  assert(on_calls == 3U);
}

static void test_replacement_and_immediate_off(void) {
  reset_device();
  output_driver_request(0xFFU);
  output_driver_service(0U);
  assert(hardware_mask == 0x01U);

  /* The latest complete mask cancels every obsolete pending activation. */
  output_driver_request(0x01U);
  output_driver_service(5000U * TICKS_PER_MS);
  assert(hardware_mask == 0x01U);
  assert(on_calls == 1U);

  /* De-energization happens during request acceptance, without a delay. */
  output_driver_request(0x00U);
  assert(hardware_mask == 0U);
  assert(off_calls == 1U);
  assert(last_off_argument == 0x01U);
}

static void test_mixed_change_preserves_interval(void) {
  reset_device();
  assert(output_driver_set_activation_delay_units(5U));
  output_driver_request(0x01U);
  output_driver_service(1000U);

  /* Output 1 turns off immediately, while output 8 remains safely pending. */
  output_driver_request(0x80U);
  assert(hardware_mask == 0U);
  output_driver_service(1000U + 499U * TICKS_PER_MS);
  assert(hardware_mask == 0U);
  output_driver_service(1000U + 500U * TICKS_PER_MS);
  assert(hardware_mask == 0x80U);
}

static void test_tick_wrap(void) {
  reset_device();
  assert(output_driver_set_activation_delay_units(5U));
  output_driver_request(0x03U);
  const uint32_t start = UINT32_MAX - 100U;
  output_driver_service(start);
  output_driver_service(start + 500U * TICKS_PER_MS);
  assert(hardware_mask == 0x03U);
}

int main(void) {
  test_defaults_and_validation();
  test_staggered_activation();
  test_replacement_and_immediate_off();
  test_mixed_change_preserves_interval();
  test_tick_wrap();
  return 0;
}
