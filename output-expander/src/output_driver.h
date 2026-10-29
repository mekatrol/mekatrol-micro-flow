#ifndef OUTPUT_EXPANDER_OUTPUT_DRIVER_H
#define OUTPUT_EXPANDER_OUTPUT_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#define OUTPUT_DRIVER_CHANNEL_COUNT 8U
#define OUTPUT_DRIVER_DELAY_UNIT_MS 100U
#define OUTPUT_DRIVER_MIN_ACTIVATION_DELAY_UNITS 5U
#define OUTPUT_DRIVER_DEFAULT_ACTIVATION_DELAY_UNITS 5U
#define OUTPUT_DRIVER_MAX_ACTIVATION_DELAY_UNITS 100U

/*
 * Initialize foreground-owned sequencing state after hal_init() has placed all
 * physical outputs in their inactive state. Initialization establishes the
 * safe 500 ms default before stored configuration is applied.
 */
void output_driver_init(void);

/*
 * Replace the complete desired logical output mask. Outputs changing from ON
 * to OFF are de-energized immediately because OFF transitions do not create
 * inrush current. Outputs changing from OFF to ON remain pending until serviced.
 * The return value is the accepted desired mask, not the applied GPIO mask.
 */
uint8_t output_driver_request(uint8_t requested_mask);

/*
 * Advance the non-blocking activation sequencer using the free-running 48 MHz
 * SysTick value. At most one physical output is energized by a call, and no
 * energization occurs until the configured interval has elapsed since the
 * preceding energization. Delayed foreground execution is safe: it never
 * causes the driver to catch up by activating several outputs together.
 */
void output_driver_service(uint32_t now_ticks);

/* Return the mask actually applied to GPIO and reported by GET_OUTPUTS. */
uint8_t output_driver_get(void);

/* Return the latest accepted target, including outputs still awaiting service. */
uint8_t output_driver_get_requested(void);

/*
 * Activation-delay configuration accessors. One unit represents 100 ms, so
 * the complete supported 500 ms through 10 second range occupies one byte in
 * protocol packets and persistent configuration storage.
 */
uint8_t output_driver_get_activation_delay_units(void);
bool output_driver_set_activation_delay_units(uint8_t delay_units);

#endif
