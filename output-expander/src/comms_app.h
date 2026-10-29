
#ifndef OUTPUT_EXPANDER_COMMS_APP_H
#define OUTPUT_EXPANDER_COMMS_APP_H

#include "command_processor.h"
#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Temporary firmware development address.
 *
 * EEPROM is not installed yet. A matching ASSIGN_ADDRESS_BY_UID
 * broadcast may change this address in RAM, but the
 * original value returns after a hardware reset.
 */
#ifndef COMMS_BOOT_ADDRESS
#define COMMS_BOOT_ADDRESS PROTOCOL_UNCOMMISSIONED_ADDRESS
#endif

#define COMMS_FIRMWARE_MAJOR 1U
#define COMMS_FIRMWARE_MINOR 0U

/*
 * RESET is protocol v1 command 0x40.
 *
 * The request has zero payload bytes.
 * The successful response also has zero payload bytes.
 *
 * The device must not initiate reset until the response
 * has completely left the USART and RS-485 DE is released.
 */
#define COMMS_COMMAND_RESET 0x40U

/*
 * Discovery is allowed to remain pending for no more than fifteen seconds.
 * Keeping the limit as a named constant makes the safety bound visible and
 * prevents a damaged or permanently busy bus from leaving a reply armed
 * forever. The randomized gap is intentionally short at 10--100 ms: it is
 * long enough to separate devices at 19,200 baud without making a normal
 * scan unnecessarily slow.
 */
#define COMMS_DISCOVERY_TIMEOUT_MS 15000U
#define COMMS_DISCOVERY_MIN_DELAY_MS 10U
#define COMMS_DISCOVERY_DELAY_SPAN_MS 91U
#define COMMS_TICKS_PER_MS UINT32_C(48000)

/*
 * Foreground-owned communications application state.
 *
 * WHAT:
 * Hold device identity, active RAM address and the
 * requested watchdog-reset state.
 *
 * WHY:
 * The application must not stop feeding the watchdog
 * until the RESET acknowledgment was transmitted.
 *
 * reset_requested is ordinary bool, not volatile:
 * no ISR reads or writes it. Both command processing
 * and watchdog-feed decisions occur in the foreground.
 */
typedef struct {
  command_processor_context_t command;
  bool reset_requested;
  bool discovery_pending;
  uint8_t discovery_sequence;
  uint32_t discovery_started_ticks;
  uint32_t discovery_reply_due_ticks;
  uint32_t discovery_random_state;
} comms_app_t;

typedef enum {
  COMMS_APP_IGNORED = 0,
  COMMS_APP_ADDRESS_CHANGED,
  COMMS_APP_RESPONSE_SENT,
  COMMS_APP_TRANSMIT_FAILED
} comms_app_result_t;

/*
 * Initialize foreground application state.
 *
 * The UID must contain the 12 factory-identity bytes
 * in protocol order.
 *
 * Valid initial addresses are 0x01..0xFE and 0xFF,
 * which represents an uncommissioned device.
 *
 * A successful initialization clears any previous
 * reset request.
 */
void comms_app_init(comms_app_t *app, uint8_t boot_address,
                    const uint8_t uid[DISPATCHER_UID_SIZE],
                    uint8_t firmware_major, uint8_t firmware_minor);

/*
 * Process one fully decoded and CRC-validated packet.
 *
 * Command processing remains in the foreground.
 *
 * A valid RESET request is acknowledged through the
 * existing blocking response transmitter. Only after
 * that transmitter returns successfully is the reset
 * request committed to application state.
 *
 * This function does not directly access watchdog
 * registers or trigger an immediate CPU reset.
 */
comms_app_result_t comms_app_process(comms_app_t *app,
                                     const protocol_packet_t *request);

/*
 * Timestamped packet processing used by the firmware foreground loop.
 * Supplying the receive-completion time lets discovery start and defer its
 * reply without blocking normal command handling or the watchdog.
 */
comms_app_result_t comms_app_process_at(comms_app_t *app,
                                        const protocol_packet_t *packet,
                                        uint32_t now_ticks);

/*
 * Give a pending discovery reply an opportunity to transmit. The caller
 * invokes this once per foreground pass after draining received frames, so a
 * reply is never started while an already received device response is still
 * waiting to be observed.
 */
comms_app_result_t comms_app_service(comms_app_t *app, uint32_t now_ticks);

/* Current commissioned address. */
uint8_t comms_app_address(const comms_app_t *app);

/*
 * Whether foreground watchdog feeding may continue.
 *
 * false means a valid RESET was acknowledged and the
 * watchdog must now expire.
 *
 * Once reset is requested, this state is latched until
 * the MCU restarts or comms_app_init() is called again.
 */
bool comms_app_watchdog_feed_allowed(const comms_app_t *app);

/*
 * Report whether this device is still waiting to answer an active discovery.
 * The status LED consumes this read-only policy state; exposing a query keeps
 * LED code from reaching into the communications state machine's internals.
 */
bool comms_app_discovery_pending(const comms_app_t *app);

#endif
