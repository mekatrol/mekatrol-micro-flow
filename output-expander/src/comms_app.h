
#ifndef OUTPUT_EXPANDER_COMMS_APP_H
#define OUTPUT_EXPANDER_COMMS_APP_H

#include "command_processor.h"
#include "protocol.h"

#include <stdint.h>

/*
 * Temporary development address.
 *
 * The EEPROM-backed address loader will replace this setting later.
 * 0x17 is a commissioned-range test address, not factory identity.
 *
 * Compile with COMMS_BOOT_ADDRESS=0xFFU to start uncommissioned.
 */
#ifndef COMMS_BOOT_ADDRESS
#define COMMS_BOOT_ADDRESS 0x17U
#endif

#define COMMS_FIRMWARE_MAJOR 1U
#define COMMS_FIRMWARE_MINOR 0U

/*
 * Foreground-owned communications state.
 *
 * The USART ISR must never read or modify this structure.
 *
 * device_address is volatile in the configuration sense:
 * RAM-only commissioning is lost on MCU reset.
 * C's volatile qualifier is neither necessary nor appropriate here,
 * because foreground code owns this state exclusively.
 */
typedef struct {
  command_processor_context_t command;
} comms_app_t;

/*
 * Result of processing a decoded request.
 *
 * IGNORED:
 *   The packet was rejected or addressed elsewhere.
 *
 * ADDRESS_CHANGED:
 *   A matching broadcast ASSIGN_UID updated the RAM address.
 *   No reply was transmitted.
 *
 * RESPONSE_SENT:
 *   A unicast response was submitted successfully to the
 *   blocking RS-485 transmitter.
 *
 * TRANSMIT_FAILED:
 *   A response was prepared, but transport rejected it.
 *   This does not establish whether anything reached the bus.
 */
typedef enum {
  COMMS_APP_IGNORED = 0,
  COMMS_APP_ADDRESS_CHANGED,
  COMMS_APP_RESPONSE_SENT,
  COMMS_APP_TRANSMIT_FAILED
} comms_app_result_t;

/*
 * Initialize application addressing, UID and firmware version.
 *
 * The caller must supply the 12 UID bytes in protocol order.
 * The boot address must be 0x01..0xFE or uncommissioned 0xFF.
 * Invalid arguments leave the application state unchanged.
 */
void comms_app_init(comms_app_t *app, uint8_t boot_address,
                    const uint8_t uid[DISPATCHER_UID_SIZE],
                    uint8_t firmware_major, uint8_t firmware_minor);

/*
 * Process a single fully decoded, CRC-validated packet.
 *
 * Must run in the foreground, never from the USART interrupt.
 *
 * Broadcast UID assignment is handled silently, with a RAM-only
 * address update. All ordinary unicast commands use the existing
 * command processor and response transmitter.
 *
 * Any physical output changes occur inside command_processor_process().
 */
comms_app_result_t comms_app_process(comms_app_t *app,
                                     const protocol_packet_t *request);

/* Return the current RAM address for diagnostics and testing. */
uint8_t comms_app_address(const comms_app_t *app);

#endif
