
#include "output_state.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Verify output-state initialization.
 *
 * WHAT:
 * Check the safe all-off state and a configured
 * nonzero startup mask.
 *
 * WHY:
 * The firmware must know the logical state that
 * corresponds to the physical output levels.
 */
static void test_initialization(void) {
  output_state_t state = {0};

  output_state_init(&state, 0x00U);
  assert(output_state_get(&state) == 0x00U);

  output_state_init(&state, 0x85U);
  assert(output_state_get(&state) == 0x85U);

  output_state_init(&state, 0xFFU);
  assert(output_state_get(&state) == 0xFFU);
}

/*
 * Verify that each of the eight logical output bits
 * is independently represented.
 *
 * WHY:
 * An incorrect bit position would activate the
 * wrong physical output when integrated with the HAL.
 */
static void test_individual_outputs(void) {
  output_state_t state;
  output_state_init(&state, 0x00U);

  for (unsigned int bit = 0U; bit < 8U; ++bit) {
    const uint8_t mask = (uint8_t)(1U << bit);

    output_state_change_t change = output_state_plan(&state, mask);

    assert(change.turn_on == mask);
    assert(change.turn_off == 0U);

    /*
     * Planning must not modify committed state.
     * Only a successful HAL operation may be followed
     * by output_state_commit().
     */
    assert(output_state_get(&state) == 0x00U);
  }
}

/*
 * Verify the transitions from all OFF to all ON
 * and back to all OFF.
 *
 * These boundary cases exercise all eight bits
 * simultaneously.
 */
static void test_all_outputs(void) {
  output_state_t state;
  output_state_init(&state, 0x00U);

  output_state_change_t change = output_state_plan(&state, 0xFFU);

  assert(change.turn_on == 0xFFU);
  assert(change.turn_off == 0x00U);

  output_state_commit(&state, 0xFFU);
  assert(output_state_get(&state) == 0xFFU);

  change = output_state_plan(&state, 0x00U);

  assert(change.turn_on == 0x00U);
  assert(change.turn_off == 0xFFU);

  output_state_commit(&state, 0x00U);
  assert(output_state_get(&state) == 0x00U);
}

/*
 * Verify a mixed transition requiring simultaneous
 * ON and OFF changes.
 *
 * Current:   0x0F = 00001111
 * Requested: 0x85 = 10000101
 *
 * Turn ON:   0x80 = 10000000
 * Turn OFF:  0x0A = 00001010
 *
 * Outputs already in the correct state are excluded.
 */
static void test_mixed_transition(void) {
  output_state_t state;
  output_state_init(&state, 0x0FU);

  const output_state_change_t change = output_state_plan(&state, 0x85U);

  assert(change.turn_on == 0x80U);
  assert(change.turn_off == 0x0AU);

  /*
   * The masks must never overlap: one output cannot
   * be commanded ON and OFF by the same transition.
   */
  assert((uint8_t)(change.turn_on & change.turn_off) == 0U);

  /* Planning must preserve the original state. */
  assert(output_state_get(&state) == 0x0FU);

  output_state_commit(&state, 0x85U);
  assert(output_state_get(&state) == 0x85U);
}

/*
 * Verify that an unchanged requested state generates
 * no output transitions.
 *
 * WHY:
 * Avoid unnecessary GPIO writes and keep the output
 * transition operation deterministic.
 */
static void test_no_change(void) {
  output_state_t state;
  output_state_init(&state, 0x85U);

  const output_state_change_t change = output_state_plan(&state, 0x85U);

  assert(change.turn_on == 0U);
  assert(change.turn_off == 0U);
  assert(output_state_get(&state) == 0x85U);
}

/*
 * Verify that the software state does not change
 * until an application explicitly commits it.
 *
 * WHY:
 * The foreground must not report requested outputs
 * as successfully applied before performing GPIO writes.
 */
static void test_commit_separation(void) {
  output_state_t state;
  output_state_init(&state, 0x00U);

  const output_state_change_t change = output_state_plan(&state, 0xA5U);

  assert(change.turn_on == 0xA5U);
  assert(change.turn_off == 0x00U);

  /* No commit: outputs must still be reported OFF. */
  assert(output_state_get(&state) == 0x00U);

  /*
   * Simulate the application confirming successful
   * hardware application.
   *
   * This is a logical-state test only; it does not
   * emulate electrical GPIO behavior.
   */
  output_state_commit(&state, 0xA5U);

  assert(output_state_get(&state) == 0xA5U);
}

/*
 * Verify defensive NULL handling.
 *
 * These checks protect portable helper functions
 * from invalid caller arguments. They do not change
 * the physical hardware state.
 */
static void test_null_arguments(void) {
  output_state_init(NULL, 0x85U);

  assert(output_state_get(NULL) == 0x00U);

  const output_state_change_t change = output_state_plan(NULL, 0x85U);

  assert(change.turn_on == 0x85U);
  assert(change.turn_off == 0x00U);

  output_state_commit(NULL, 0x85U);
}

/*
 * Exercise every possible pair of eight-bit masks.
 *
 * WHAT:
 * Verify all 65,536 current/requested combinations.
 *
 * WHY:
 * Eight outputs have only 256 possible states, so the
 * host can exhaustively verify the transition algorithm.
 *
 * This catches bitwise errors that a few hand-picked
 * examples might miss.
 *
 * The loop uses unsigned int to avoid uint8_t wrapping
 * back to zero and accidentally running forever.
 */
static void test_all_transitions(void) {
  output_state_t state;

  for (unsigned int current = 0U; current < 256U; ++current) {
    output_state_init(&state, (uint8_t)current);

    for (unsigned int requested = 0U; requested < 256U; ++requested) {

      const uint8_t target = (uint8_t)requested;

      const output_state_change_t change = output_state_plan(&state, target);

      /*
       * Neither transition mask may contain bits
       * belonging to the wrong final state.
       */
      assert((uint8_t)(change.turn_on & (uint8_t)~target) == 0U);

      assert((uint8_t)(change.turn_off & target) == 0U);

      assert((uint8_t)(change.turn_on & change.turn_off) == 0U);

      /*
       * Applying the logical ON and OFF operations
       * must produce exactly the requested mask.
       */
      const uint8_t result = (uint8_t)(((uint8_t)current | change.turn_on) &
                                       (uint8_t)~change.turn_off);

      assert(result == target);

      /*
       * Planning a transition cannot commit it.
       * The current mask must remain unchanged.
       */
      assert(output_state_get(&state) == (uint8_t)current);
    }
  }
}

/*
 * Execute all portable output-state tests.
 *
 * CTest reports assertion failures through the
 * executable's nonzero exit status.
 *
 * The host-debug build also runs AddressSanitizer
 * and UndefinedBehaviorSanitizer.
 */
int main(void) {
  test_initialization();
  test_individual_outputs();
  test_all_outputs();
  test_mixed_transition();
  test_no_change();
  test_commit_separation();
  test_null_arguments();
  test_all_transitions();

  return 0;
}
