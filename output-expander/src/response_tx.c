
#include "response_tx.h"

#include "dispatcher.h"
#include "protocol.h"
#include "usart.h"

#include <stddef.h>
#include <stdint.h>

/*
 * One complete protocol v1 wire frame requires at most
 * 34 bytes, including the final COBS delimiter.
 *
 * WHAT:
 * Allocate bounded temporary storage on the stack.
 *
 * WHY:
 * The CH32V003 has limited SRAM and no heap allocation.
 * The existing protocol encoder defines the maximum
 * frame size and handles CRC calculation and COBS.
 *
 * No separate CRC or COBS implementation is needed here.
 */
bool response_tx_send(const protocol_packet_t *packet) {
  /*
   * A missing packet cannot be encoded or transmitted.
   *
   * This check occurs before dereferencing the pointer.
   */
  if (packet == NULL) {
    return false;
  }

  /*
   * Transmit only response or error packet types.
   *
   * WHAT:
   * Reject request packets and undefined packet types.
   *
   * WHY:
   * This device follows a single-master protocol.
   * It must never originate a request or unsolicited
   * transmission through the response pathway.
   */
  if (packet->type != PROTOCOL_TYPE_RESPONSE &&
      packet->type != PROTOCOL_TYPE_ERROR) {
    return false;
  }

  /*
   * Response address restrictions
   * -----------------------------
   * Broadcast address (PROTOCOL_BROADCAST_ADDRESS):
   *   Never transmit a response. Multiple devices may receive and process
   *   broadcast messages, so responding could cause bus contention.
   *
   * Uncommissioned address (PROTOCOL_UNCOMMISSIONED_ADDRESS / 0xFF):
   *   Only permit a successful PING information response. This allows a
   *   single isolated, uncommissioned device to be discovered before its
   *   unique address is assigned using its UID.
   *
   * All other destination addresses:
   *   Responses are permitted, subject to normal address validation.
   */
  if (packet->destination == PROTOCOL_BROADCAST_ADDRESS ||
      (packet->destination == PROTOCOL_UNCOMMISSIONED_ADDRESS &&
       (packet->type != PROTOCOL_TYPE_RESPONSE ||
        packet->command != DISPATCHER_COMMAND_PING ||
        packet->payload_length != DISPATCHER_PING_RESPONSE_SIZE))) {
    return false;
  }
  /*
   * Check the fixed payload capacity before encoding.
   *
   * WHY:
   * A packet produced by a correctly implemented
   * command processor already respects this limit.
   *
   * Keeping the check here ensures a future caller
   * cannot accidentally supply an oversized response.
   */
  if (packet->payload_length > PROTOCOL_MAX_PAYLOAD) {
    return false;
  }

  /*
   * Protocol v1 requires exactly one payload byte
   * containing an error code for an error response.
   *
   * WHY:
   * An empty error response would not identify the
   * failure, and a longer error payload would violate
   * the protocol's current response structure.
   *
   * The exact error-code values are chosen by the
   * command processor, not by this transport module.
   */
  if (packet->type == PROTOCOL_TYPE_ERROR && packet->payload_length != 1U) {
    return false;
  }

  /*
   * Encode into private bounded stack storage.
   *
   * WHAT:
   * protocol_encode() adds:
   *
   * - Protocol version and packet header
   * - Payload bytes
   * - CRC-16/CCITT-FALSE in little-endian order
   * - COBS encoding
   * - Final 0x00 frame delimiter
   *
   * WHY:
   * The existing encoder is already unit-tested.
   * Reusing it preserves interoperability and avoids
   * another implementation of the wire format.
   */
  uint8_t frame[PROTOCOL_MAX_ENCODED_SIZE];
  size_t frame_length = 0U;

  const protocol_result_t result =
      protocol_encode(packet, frame, sizeof(frame), &frame_length);

  /*
   * A failed encode must never produce a partial
   * response on the physical bus.
   *
   * The encoder sets frame_length to zero on error.
   * Check both its return value and resulting length
   * before enabling the RS-485 transmitter.
   *
   * A valid protocol frame also cannot exceed the
   * reserved array capacity.
   */
  if (result != PROTOCOL_OK || frame_length == 0U ||
      frame_length > sizeof(frame)) {
    return false;
  }

  /*
   * Verify the expected delimiter defensively.
   *
   * WHY:
   * The protocol relies on 0x00 to establish an
   * unambiguous end of frame. A missing delimiter
   * could cause the master to wait indefinitely
   * or combine subsequent traffic into one frame.
   *
   * The normal protocol encoder always appends it.
   */
  if (frame[frame_length - 1U] != 0U) {
    return false;
  }

  /*
   * Send the entire frame through the existing USART
   * hardware driver.
   *
   * WHAT:
   * usart_write_response() performs the following:
   *
   * 1. Waits for an idle USART transmitter.
   * 2. Raises PC6 to enable the RS-485 line driver.
   * 3. Waits for transceiver driver-enable settling.
   * 4. Writes all frame bytes using the USART TXE flag.
   * 5. Waits for TC after the final stop bit.
   * 6. Lowers PC6 to release the RS-485 bus.
   *
   * WHY:
   * TXE only means the transmit data register can
   * accept another byte. Releasing DE at TXE could
   * truncate the final character.
   *
   * TC confirms that the final stop bit has completed
   * before the transceiver returns to receive mode.
   *
   * The driver is blocking, so this local frame buffer
   * remains valid throughout the transmission.
   *
   * No transmit timeout is implemented in the existing
   * driver. A stuck USART status flag could therefore
   * block indefinitely; this is a known hardware
   * limitation and is not changed by this increment.
   */
  usart_write_response(frame, frame_length);

  return true;
}
