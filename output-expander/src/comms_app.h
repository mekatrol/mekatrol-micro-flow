
#ifndef OUTPUT_EXPANDER_COMMS_APP_H
#define OUTPUT_EXPANDER_COMMS_APP_H

#include "command_processor.h"
#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Temporary firmware development address.
 *
 * EEPROM is not installed yet. A matching ASSIGN_UID
 * broadcast may change this address in RAM, but the
 * original value returns after a hardware reset.
 */
#ifndef COMMS_BOOT_ADDRESS
#define COMMS_BOOT_ADDRESS 0x17U
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

/* Current RAM-only commissioned address. */
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

#endif
