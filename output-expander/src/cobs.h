
#ifndef OUTPUT_EXPANDER_COBS_H
#define OUTPUT_EXPANDER_COBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * COBS framing limits for protocol version 1.
 *
 * A decoded packet contains:
 *   6-byte header
 *   Up to 24 payload bytes
 *   2-byte CRC
 *
 * COBS adds at most one byte for packets of this
 * size. The trailing zero delimiter is handled
 * separately by the protocol frame receiver.
 */
#define COBS_MAX_DECODED_SIZE 32U
#define COBS_MAX_ENCODED_SIZE 33U
#define COBS_MAX_FRAME_SIZE 34U

/*
 * Encode arbitrary bytes into a zero-free COBS body.
 *
 * WHAT:
 * Replaces zero bytes with length-code boundaries.
 *
 * WHY:
 * The RS-485 protocol uses 0x00 to indicate the end
 * of a frame. Encoded packet data must therefore
 * contain no zero bytes.
 *
 * Parameters:
 *   input:           Raw packet bytes.
 *   input_length:    Number of raw bytes.
 *   output:          Destination buffer.
 *   output_capacity: Available destination bytes.
 *   output_length:   Receives encoded byte count.
 *
 * The output does NOT include the 0x00 delimiter.
 *
 * Returns false if a pointer is invalid, the input
 * exceeds the protocol limit, or the output buffer
 * is too small.
 *
 * Input and output buffers must not overlap.
 */
bool cobs_encode(const uint8_t *input, size_t input_length, uint8_t *output,
                 size_t output_capacity, size_t *output_length);

/*
 * Decode a COBS body into its original bytes.
 *
 * WHAT:
 * Restores zero bytes represented by COBS codes.
 *
 * WHY:
 * The protocol dispatcher requires the original
 * header, payload and CRC bytes for validation.
 *
 * The encoded input must exclude the final
 * zero delimiter.
 *
 * Rejects:
 *   Empty encoded bodies
 *   Zero bytes inside the encoded body
 *   Invalid COBS length codes
 *   Output buffer overflow
 *   Decoded packets larger than 32 bytes
 *
 * Input and output buffers must not overlap.
 */
bool cobs_decode(const uint8_t *input, size_t input_length, uint8_t *output,
                 size_t output_capacity, size_t *output_length);

#endif
