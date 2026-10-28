
#ifndef OUTPUT_EXPANDER_WATCHDOG_H
#define OUTPUT_EXPANDER_WATCHDOG_H

/*
 * Independent watchdog for the CH32V003.
 *
 * The IWDG runs from the internal low-speed oscillator,
 * independently of the 48 MHz system clock.
 *
 * Once started by software, it cannot be disabled until
 * a device reset. A RESET command is implemented by
 * deliberately stopping watchdog refreshes.
 *
 * All calls originate in foreground context, never an ISR.
 */

/*
 * Configure and start the watchdog.
 *
 * Prescaler: 128
 * Reload:    4095
 * Nominal LSI frequency: 128 kHz
 * Nominal expiration interval: approximately 4.096 s
 *
 * Must be called only once, after normal system
 * initialization, just before entering the main loop.
 *
 * Assumes normal software-controlled IWDG option-byte mode.
 * Initialization waits for LSI and watchdog register updates.
 */
void watchdog_start(void);

/*
 * Refresh the watchdog counter.
 *
 * Call as the final operation of a successfully completed
 * foreground loop iteration.
 *
 * Never call from an interrupt: doing so could conceal a
 * stalled foreground application.
 */
void watchdog_feed(void);

#endif
