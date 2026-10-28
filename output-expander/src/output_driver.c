
#include "output_driver.h"

#include "hal.h"
#include "output_state.h"

#include <stdint.h>

/*
 * Single foreground-owned output state.
 *
 * WHAT:
 * Store the last logical output mask commanded through
 * the driver.
 *
 * WHY:
 * The protocol needs a consistent eight-channel state,
 * but reading electrical GPIO levels would require
 * reversing the active-low polarity and could be
 * affected by external pin conditions.
 *
 * No volatile qualifier is necessary because this
 * object is not shared with the USART ISR.
 */
static output_state_t output_state;

void output_driver_init(void) {
  /*
   * hal_init(0xFF) has already configured every physical
   * output to its inactive electrical level.
   *
   * Recording zero here makes the software state match
   * those pre-established physical output levels.
   *
   * Do not repeat GPIO initialization: that would
   * unnecessarily modify working hardware startup code.
   */
  output_state_init(&output_state, 0U);
}

uint8_t output_driver_set(uint8_t requested_mask) {
  /*
   * Calculate changes relative to the last applied mask.
   *
   * Only channels whose logical state changes are included
   * in the plan. All other channels remain untouched.
   */
  const output_state_change_t change =
      output_state_plan(&output_state, requested_mask);

  /*
   * Turn OFF selected outputs before turning others ON.
   *
   * WHAT:
   * Apply all required OFF transitions first.
   *
   * WHY:
   * When switching between mutually exclusive loads,
   * removing the old outputs before enabling new outputs
   * avoids a temporary state where both are commanded ON.
   *
   * The HAL handles active-low polarity. Passing a set
   * bit here means logically OFF, not electrically LOW.
   *
   * A zero change mask does not require a GPIO write.
   */
  if (change.turn_off != 0U) {
    hal_output_off(change.turn_off);
  }

  /*
   * Apply the required ON transitions.
   *
   * The HAL uses GPIOx_BSHR set/reset operations,
   * changing only the selected physical GPIO pins.
   *
   * It does not disturb the RS-485 direction pin PC6
   * or the SWIO debugging pin PD1.
   */
  if (change.turn_on != 0U) {
    hal_output_on(change.turn_on);
  }

  /*
   * Record the logical mask after issuing the GPIO writes.
   *
   * The existing HAL functions return void and provide
   * no independent electrical readback or error indication.
   *
   * Therefore, this commit means that the GPIO operations
   * have been issued, not that relay contacts or external
   * loads have been physically verified.
   */
  output_state_commit(&output_state, requested_mask);

  return output_state_get(&output_state);
}

uint8_t output_driver_get(void) {
  /*
   * Expose the foreground-owned logical output state.
   *
   * The returned value is suitable for GET_OUTPUTS
   * and SET_OUTPUTS response construction.
   */
  return output_state_get(&output_state);
}
