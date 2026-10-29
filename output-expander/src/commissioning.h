
#ifndef OUTPUT_EXPANDER_COMMISSIONING_H
#define OUTPUT_EXPANDER_COMMISSIONING_H

#include "dispatcher.h"
#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Protocol v1 ASSIGN_ADDRESS_BY_UID request layout.
 *
 * WHAT:
 * The broadcast commissioning payload contains the
 * 12 raw UID bytes followed by one new address byte.
 *
 * WHY:
 * A device may accept a commissioning request only
 * when every UID byte matches its factory identity.
 * The new address cannot be inferred from the packet
 * destination because ASSIGN_ADDRESS_BY_UID uses broadcast 0x00.
 *
 * The UID byte order must be the same order used by
 * PING and by the protocol contract.
 */
#define COMMISSIONING_UID_SIZE DISPATCHER_UID_SIZE
#define COMMISSIONING_ADDRESS_OFFSET COMMISSIONING_UID_SIZE
#define COMMISSIONING_PAYLOAD_SIZE (COMMISSIONING_UID_SIZE + 1U)

/*
 * Commissioned addresses are 0x01 through 0xFE.
 *
 * Address 0x00 is reserved for broadcast.
 * Address 0xFF represents the uncommissioned state.
 */
#define COMMISSIONING_MIN_ADDRESS 0x01U
#define COMMISSIONING_MAX_ADDRESS 0xFEU

/*
 * Validate a proposed commissioned address.
 *
 * WHAT:
 * Return true only for addresses in 0x01..0xFE.
 *
 * WHY:
 * Reserved addresses must never become persistent
 * unicast identities.
 *
 * This helper has no hardware or storage side effects.
 */
bool commissioning_address_valid(uint8_t address);

/*
 * Validate one decoded broadcast ASSIGN_ADDRESS_BY_UID request.
 *
 * WHAT:
 * Verify that the packet:
 *
 * - Is a request packet.
 * - Is addressed to broadcast 0x00.
 * - Uses command ASSIGN_ADDRESS_BY_UID (0x30).
 * - Contains exactly 13 payload bytes.
 * - Matches the local UID in all 12 positions.
 * - Requests a commissioned address in 0x01..0xFE.
 *
 * WHY:
 * All devices on the RS-485 bus receive a broadcast.
 * Only the device with the matching physical UID may
 * proceed towards persistent address assignment.
 *
 * The packet must already have passed COBS, version,
 * structural and CRC validation in protocol_decode().
 * This function does not repeat CRC verification.
 *
 * local_uid points to the 12 raw UID bytes provided
 * by the hardware-specific identity layer.
 *
 * On success:
 * - Returns true.
 * - Writes the validated address to *new_address.
 *
 * On failure:
 * - Returns false.
 * - Leaves *new_address unchanged.
 *
 * All three arguments must be non-NULL.
 *
 * This function does not:
 * - Write EEPROM.
 * - Change the currently active device address.
 * - Generate any response packet.
 * - Control GPIO or RS-485 DE.
 * - Execute inside an interrupt.
 *
 * The caller must commit the new address to EEPROM
 * and verify it before activating the address in RAM.
 * Broadcast commissioning must remain silent even
 * after a successful persistent commit.
 */
bool commissioning_validate_assign_address_by_uid(
    const protocol_packet_t *request,
    const uint8_t local_uid[COMMISSIONING_UID_SIZE], uint8_t *new_address);

#endif
