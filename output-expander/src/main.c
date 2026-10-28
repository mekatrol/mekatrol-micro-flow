#include "hal.h"

#include <stdint.h>

int main(void) {
  /* This test board connects all eight LEDs to the supply through resistors,
   * so their GPIO pins sink current: LOW = LED ON, HIGH = LED OFF.
   * Configure every output as active-low and preload all LEDs OFF before
   * their GPIO pins switch into output mode. */
  hal_init(0xFFU);

  /* Blink logical output 1 (bit 0). Selecting only this bit ensures the
   * other seven outputs stay OFF throughout the test. */
  const uint8_t output_mask = 0x01U;

  /* hal_init() starts every output logically OFF, so the first iteration
   * must turn the selected LED ON. Track the logical LED state rather than
   * the electrical GPIO level, which depends on active_low_mask. */
  uint8_t output_is_on = 0U;

  for (;;) {
    /* A blocking half-second delay gives a visible 1 Hz blink cycle. */
    hal_delay_half_second();

    if (output_is_on != 0U) {
      /* The LED is ON: switch it OFF and update the tracked logical state. */
      hal_output_off(output_mask);
      output_is_on = 0U;
    } else {
      /* The LED is OFF: switch it ON and update the tracked logical state. */
      hal_output_on(output_mask);
      output_is_on = 1U;
    }
  }
}