#ifndef MICROFLOW_RASPBERRY_PI_PICO_FIRMWARE_CONFIG_H
#define MICROFLOW_RASPBERRY_PI_PICO_FIRMWARE_CONFIG_H

/*
 * Raspberry Pi Pico firmware configuration
 *
 * These settings control board-level timing, scheduling, communications, and
 * statically allocated USB buffering. Change the defaults here when producing
 * a firmware variant, or define a setting on the compiler command line to
 * override it without modifying this file. All time values are expressed in
 * microseconds (us) unless the setting explicitly states another unit.
 */

/*
 * Interval between heartbeat timer events, in microseconds (us).
 *
 * Each event toggles the status indication and gives the status service an
 * opportunity to expire an incomplete UART upload. A smaller value makes the
 * LED and timeout servicing more responsive but wakes the CPU more often. A
 * larger value reduces wakeups but slows the visible heartbeat and can delay
 * upload-expiration checks. This value must be greater than zero.
 */
#ifndef MICROFLOW_HEARTBEAT_PERIOD_US
#define MICROFLOW_HEARTBEAT_PERIOD_US 500000U
#endif

/*
 * Maximum time without main-loop progress before the hardware watchdog resets
 * the controller, in microseconds (us).
 *
 * Reduce this value to recover faster from stalled firmware. Increase it if a
 * legitimate supervisor pass can take longer. It must remain comfortably
 * greater than the longest expected service execution time so normal work does
 * not cause unintended resets. This value must be greater than zero.
 */
#ifndef MICROFLOW_WATCHDOG_TIMEOUT_US
#define MICROFLOW_WATCHDOG_TIMEOUT_US 2000000U
#endif

/*
 * Maximum intended runtime of one status-service dispatch, in microseconds
 * (us).
 *
 * The supervisor reports a timing fault when status work takes longer than
 * this budget. Lower values detect latency regressions more aggressively;
 * higher values tolerate slower status processing but permit it to delay other
 * cooperative services for longer. This is a monitoring budget, not a forced
 * execution deadline. Set it to zero only to disable runtime-budget checking
 * for this service.
 */
#ifndef MICROFLOW_STATUS_SERVICE_BUDGET_US
#define MICROFLOW_STATUS_SERVICE_BUDGET_US 2000U
#endif

/*
 * Maximum intended runtime of one UART upload-service dispatch, in
 * microseconds (us).
 *
 * The supervisor reports a timing fault when parsing received upload data and
 * preparing replies exceeds this budget. A smaller value exposes excessive
 * UART work sooner; a larger value permits bigger bursts before a fault is
 * reported, at the cost of allowing other cooperative services to wait longer.
 * This does not set the UART speed. Set it to zero only to disable runtime-
 * budget checking for this service.
 */
#ifndef MICROFLOW_UART_SERVICE_BUDGET_US
#define MICROFLOW_UART_SERVICE_BUDGET_US 5000U
#endif

/*
 * Maximum intended runtime of one USB CDC service dispatch, in microseconds
 * (us).
 *
 * The supervisor reports a timing fault when polling USB, moving received
 * bytes, and processing console input exceeds this budget. Lower values detect
 * unexpectedly long USB work sooner; higher values tolerate larger transfers
 * while allowing more delay to other cooperative services. This is not a USB
 * transfer timeout. Set it to zero only to disable runtime-budget checking for
 * this service.
 */
#ifndef MICROFLOW_USB_SERVICE_BUDGET_US
#define MICROFLOW_USB_SERVICE_BUDGET_US 5000U
#endif

/*
 * UART0 signalling rate, in bits per second (baud).
 *
 * This is the wire speed used by the binary flow-upload transport. The host
 * uploader must use the same rate. Raising it shortens transfers but makes
 * signal quality, cabling, and clock accuracy more important; lowering it
 * improves tolerance at the cost of slower uploads. Framing remains fixed by
 * the RP2040 UART driver and is not selected by this setting.
 */
#ifndef MICROFLOW_UART_BAUD_RATE
#define MICROFLOW_UART_BAUD_RATE 115200U
#endif

/*
 * Maximum idle interval permitted in a partially received UART upload, in
 * microseconds (us).
 *
 * When no additional bytes arrive for this long, the next status-service pass
 * abandons the incomplete frame so a new upload can start cleanly. A smaller
 * value recovers sooner from interrupted transfers but may reject valid data
 * from a slow or heavily delayed sender. Expiration is checked at the heartbeat
 * interval, so actual detection can occur up to one heartbeat period later.
 * This value must be greater than zero.
 */
#ifndef MICROFLOW_UART_RECEIVE_TIMEOUT_US
#define MICROFLOW_UART_RECEIVE_TIMEOUT_US 1000000U
#endif

/*
 * Number of bytes reserved for each USB CDC software queue.
 *
 * One statically allocated queue of this size is used for received data and a
 * second queue is used for transmitted data. Increasing the value absorbs
 * larger bursts and reduces dropped bytes, but consumes the same additional
 * amount of RAM in both directions (twice the increase in this value). Reducing
 * it saves RAM but increases the chance of data loss when the host or firmware
 * cannot service USB promptly. This value must be greater than zero and must
 * fit in the addressable static-memory budget for the target board.
 */
#ifndef MICROFLOW_USB_QUEUE_CAPACITY
#define MICROFLOW_USB_QUEUE_CAPACITY 256U
#endif

#endif
