
#ifndef OUTPUT_EXPANDER_OUTPUT_STATE_H
#define OUTPUT_EXPANDER_OUTPUT_STATE_H

#include <stdint.h>

/*
 * Portable logical state of the eight output channels.
 *
 * WHAT:
 * Store the last successfully applied output mask.
 *
 * WHY:
 * The protocol expresses output states as logical bits,
 * while the CH32V003 hardware uses active-low GPIO pins.
 *
 * Separating these representations keeps protocol code
 * independent of electrical polarity and pin mapping.
 *
 * Bit 0 = output channel 1.
 * Bit 7 = output channel 8.
 * A set bit means logically ON.
 *
 * This object is owned by the foreground application.
 * The USART interrupt must never modify it.
 */
typedef struct {
  uint8_t mask;
} output_state_t;

/*
 * Describe the GPIO operations required to change state.
 *
 * turn_on contains outputs transitioning from OFF to ON.
 * turn_off contains outputs transitioning from ON to OFF.
 *
 * The two masks never share a set bit.
 *
 * Hardware code applies these masks using the existing
 * polarity-aware HAL functions.
 */
typedef struct {
  uint8_t turn_on;
  uint8_t turn_off;
} output_state_change_t;

/*
 * Initialize the logical state.
 *
 * The caller must first establish the corresponding
 * physical GPIO state using the HAL.
 *
 * Initialization itself performs no hardware access.
 */
void output_state_init(output_state_t *state, uint8_t initial_mask);

/*
 * Return the last successfully committed logical mask.
 *
 * NULL returns zero as a defensive fallback.
 * The application must not use NULL as normal state.
 */
uint8_t output_state_get(const output_state_t *state);

/*
 * Calculate changes required to reach a requested mask.
 *
 * WHAT:
 * Compare the current and requested masks to determine
 * which channels need an ON or OFF transition.
 *
 * WHY:
 * Unchanged outputs need not be written again.
 * This also avoids disturbing unrelated GPIO pins.
 *
 * The function does not change state or hardware.
 *
 * NULL state is treated as all OFF for calculation.
 */
output_state_change_t output_state_plan(const output_state_t *state,
                                        uint8_t requested_mask);

/*
 * Commit a successfully applied logical output mask.
 *
 * WHAT:
 * Record the mask after the foreground has completed
 * the corresponding physical GPIO operations.
 *
 * WHY:
 * GET_OUTPUTS must report the applied output state,
 * not merely the most recent requested value.
 *
 * This function must not be called before hardware
 * application succeeds.
 *
 * No GPIO access occurs here.
 */
void output_state_commit(output_state_t *state, uint8_t applied_mask);

#endif
