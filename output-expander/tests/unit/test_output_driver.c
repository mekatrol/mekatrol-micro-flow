
#include "hal.h"
#include "output_driver.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Simulated hardware state and operation trace.
 *
 * WHAT:
 * Record the logical mask of outputs driven by mock
 * HAL operations and the order of those operations.
 *
 * WHY:
 * Host tests cannot access real CH32V003 GPIO registers.
 * Replacing only the HAL functions allows the production
 * output driver to be tested without changing its code.
 *
 * This simulates calls to the HAL, not electrical timing,
 * physical voltages or relay-contact feedback.
 */
typedef struct {
  uint8_t mask;
  uint8_t on_calls;
  uint8_t off_calls;
  uint8_t on_argument;
  uint8_t off_argument;
  uint8_t operation_count;
  uint8_t operations[2];
} fake_hal_t;

static fake_hal_t fake_hal;

/*
 * Mock the existing HAL output operations.
 *
 * These functions have the exact signatures declared
 * in hal.h, so output_driver.c can link against them
 * instead of the real register-level implementation.
 *
 * Each operation updates a simulated logical mask.
 */
void hal_output_on(uint8_t output_mask) {
  fake_hal.on_calls++;
  fake_hal.on_argument = output_mask;

  assert(fake_hal.operation_count < 2U);
  fake_hal.operations[fake_hal.operation_count++] = 1U;

  fake_hal.mask = (uint8_t)(fake_hal.mask | output_mask);
}

void hal_output_off(uint8_t output_mask) {
  fake_hal.off_calls++;
  fake_hal.off_argument = output_mask;

  assert(fake_hal.operation_count < 2U);
  fake_hal.operations[fake_hal.operation_count++] = 2U;

  fake_hal.mask = (uint8_t)(fake_hal.mask & (uint8_t)~output_mask);
}

/*
 * Reset the mock hardware's recorded operations.
 *
 * Preserve the simulated output mask unless explicitly
 * resetting the entire simulated device.
 */
static void clear_operations(void) {
  fake_hal.on_calls = 0U;
  fake_hal.off_calls = 0U;
  fake_hal.on_argument = 0U;
  fake_hal.off_argument = 0U;
  fake_hal.operation_count = 0U;
  fake_hal.operations[0] = 0U;
  fake_hal.operations[1] = 0U;
}

/*
 * Reset the mock device to all outputs OFF.
 *
 * The real firmware performs hal_init() before
 * output_driver_init(). The mock starts at the
 * same logical state without configuring registers.
 */
static void reset_device(void) {
  fake_hal = (fake_hal_t){0};
  output_driver_init();
}

/*
 * Initialization must leave the outputs OFF and must
 * not produce unnecessary GPIO writes.
 */
static void test_initialization(void) {
  reset_device();

  assert(output_driver_get() == 0x00U);
  assert(fake_hal.mask == 0x00U);
  assert(fake_hal.on_calls == 0U);
  assert(fake_hal.off_calls == 0U);
}

/*
 * A single requested ON mask must be written to the HAL.
 *
 * Logical values are preserved without active-low
 * inversion; physical polarity belongs to hal.c.
 */
static void test_turn_on(void) {
  reset_device();

  assert(output_driver_set(0x85U) == 0x85U);

  assert(output_driver_get() == 0x85U);
  assert(fake_hal.mask == 0x85U);

  assert(fake_hal.on_calls == 1U);
  assert(fake_hal.on_argument == 0x85U);
  assert(fake_hal.off_calls == 0U);
}

/*
 * A requested OFF transition must clear only selected
 * outputs while preserving all others.
 */
static void test_turn_off(void) {
  reset_device();

  output_driver_set(0xFFU);
  clear_operations();

  assert(output_driver_set(0x85U) == 0x85U);

  assert(fake_hal.mask == 0x85U);
  assert(output_driver_get() == 0x85U);

  /*
   * 0xFF to 0x85 requires turning OFF exactly
   * the bits represented by 0x7A.
   */
  assert(fake_hal.off_calls == 1U);
  assert(fake_hal.off_argument == 0x7AU);
  assert(fake_hal.on_calls == 0U);
}

/*
 * Verify mixed transitions and operation order.
 *
 * Current:   0x0F
 * Requested: 0x85
 *
 * Turn OFF:  0x0A
 * Turn ON:   0x80
 *
 * OFF must happen before ON to avoid temporarily
 * enabling outputs that should not be ON together.
 */
static void test_mixed_transition(void) {
  reset_device();

  output_driver_set(0x0FU);
  clear_operations();

  assert(output_driver_set(0x85U) == 0x85U);

  assert(fake_hal.off_calls == 1U);
  assert(fake_hal.on_calls == 1U);

  assert(fake_hal.off_argument == 0x0AU);
  assert(fake_hal.on_argument == 0x80U);

  assert(fake_hal.operation_count == 2U);
  assert(fake_hal.operations[0] == 2U);
  assert(fake_hal.operations[1] == 1U);

  assert(fake_hal.mask == 0x85U);
  assert(output_driver_get() == 0x85U);
}

/*
 * Reapplying an unchanged state must not issue
 * redundant GPIO writes.
 *
 * WHY:
 * Unnecessary writes can complicate timing analysis
 * and make output-state debugging less predictable.
 */
static void test_no_change(void) {
  reset_device();

  output_driver_set(0x85U);
  clear_operations();

  assert(output_driver_set(0x85U) == 0x85U);

  assert(fake_hal.on_calls == 0U);
  assert(fake_hal.off_calls == 0U);
  assert(fake_hal.operation_count == 0U);
}

/*
 * Test every possible starting and requested mask.
 *
 * WHAT:
 * Exercise all 65,536 output-state transitions
 * through the actual production output driver.
 *
 * WHY:
 * This verifies that every bit is preserved and that
 * changing one output never changes unrelated outputs.
 *
 * There are at most two HAL operations per transition:
 * one OFF operation followed by one ON operation.
 */
static void test_all_transitions(void) {
  for (unsigned int start = 0U; start < 256U; ++start) {
    for (unsigned int target = 0U; target < 256U; ++target) {

      reset_device();

      /*
       * Establish the starting mask through the driver
       * so logical and simulated hardware states agree.
       */
      output_driver_set((uint8_t)start);
      clear_operations();

      const uint8_t requested = (uint8_t)target;
      const uint8_t applied = output_driver_set(requested);

      assert(applied == requested);
      assert(output_driver_get() == requested);
      assert(fake_hal.mask == requested);

      /*
       * Verify no unnecessary or incorrect transitions.
       */
      const uint8_t expected_off =
          (uint8_t)((uint8_t)start & (uint8_t)~requested);

      const uint8_t expected_on =
          (uint8_t)(requested & (uint8_t) ~(uint8_t)start);

      assert(fake_hal.off_argument == expected_off);
      assert(fake_hal.on_argument == expected_on);

      assert(fake_hal.off_calls == (expected_off != 0U ? 1U : 0U));

      assert(fake_hal.on_calls == (expected_on != 0U ? 1U : 0U));

      /*
       * A mixed transition must always execute
       * the OFF operation before the ON operation.
       */
      if (expected_off != 0U && expected_on != 0U) {
        assert(fake_hal.operation_count == 2U);
        assert(fake_hal.operations[0] == 2U);
        assert(fake_hal.operations[1] == 1U);
      }
    }
  }
}

/*
 * Run every test under the existing host sanitizer
 * configuration with strict compilation warnings.
 */
int main(void) {
  test_initialization();
  test_turn_on();
  test_turn_off();
  test_mixed_transition();
  test_no_change();
  test_all_transitions();

  return 0;
}
