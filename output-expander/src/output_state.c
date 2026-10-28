
#include "output_state.h"

#include <stddef.h>
#include <stdint.h>

void output_state_init(output_state_t *state, uint8_t initial_mask) {

  /*
   * Avoid dereferencing an invalid pointer.
   *
   * Initialization is a software-state operation only.
   * The caller must establish matching GPIO levels
   * separately before making this state available.
   */
  if (state == NULL) {
    return;
  }

  state->mask = initial_mask;
}

uint8_t output_state_get(const output_state_t *state) {
  /*
   * A missing state object cannot provide a trustworthy
   * output mask. Return the safe logical OFF representation
   * rather than accessing invalid memory.
   *
   * This is defensive behavior, not a hardware action.
   */
  if (state == NULL) {
    return 0U;
  }

  return state->mask;
}

output_state_change_t output_state_plan(const output_state_t *state,
                                        uint8_t requested_mask) {

  /*
   * Determine the current logical mask.
   *
   * State changes are calculated using protocol bits,
   * not physical GPIO levels.
   */
  const uint8_t current_mask = output_state_get(state);

  /*
   * Identify outputs whose requested state differs
   * from the currently applied state.
   *
   * XOR produces a set bit only where the state changes.
   */
  const uint8_t changed = (uint8_t)(current_mask ^ requested_mask);

  /*
   * A changed output must turn ON when its requested
   * bit is set, otherwise it must turn OFF.
   *
   * Cast the complement back to uint8_t because C
   * integer promotion widens operands of bitwise NOT.
   *
   * These masks contain logical channel bits only.
   * The HAL handles active-low electrical conversion.
   */
  output_state_change_t plan = {
      .turn_on = (uint8_t)(changed & requested_mask),
      .turn_off = (uint8_t)(changed & (uint8_t)~requested_mask)};

  return plan;
}

void output_state_commit(output_state_t *state, uint8_t applied_mask) {

  /*
   * Update the logical state only when the foreground
   * confirms that the corresponding hardware operation
   * has completed successfully.
   *
   * Keeping this separate from output_state_plan()
   * prevents a requested mask from being mistaken
   * for an applied mask.
   */
  if (state == NULL) {
    return;
  }

  state->mask = applied_mask;
}
