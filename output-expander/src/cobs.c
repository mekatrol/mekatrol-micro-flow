
#include "cobs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Encode raw packet bytes using COBS.
 *
 * For protocol version 1, input is limited to
 * 32 bytes. This avoids the special 254-byte
 * continuation case required by larger packets.
 */
bool cobs_encode(const uint8_t *input, size_t input_length, uint8_t *output,
                 size_t output_capacity, size_t *output_length) {
  /*
   * A COBS body always contains at least one
   * code byte, even when the decoded input
   * is empty.
   *
   * Initialize the result length to zero so
   * callers cannot mistake a failed operation
   * for a successful encoded packet.
   */
  if (output_length == NULL) {
    return false;
  }

  *output_length = 0U;

  /*
   * Validate buffer pointers and protocol limits
   * before writing anything.
   */
  if (output == NULL || (input == NULL && input_length != 0U) ||
      input_length > COBS_MAX_DECODED_SIZE || output_capacity == 0U) {
    return false;
  }

  /*
   * Reserve the first byte for the COBS code.
   *
   * Each code describes the number of bytes
   * in a nonzero run, including the code byte.
   *
   * A code of 1 means the run contains no
   * nonzero data bytes.
   */
  size_t write_index = 1U;
  size_t code_index = 0U;
  uint8_t code = 1U;

  for (size_t i = 0U; i < input_length; ++i) {
    if (input[i] == 0U) {
      /*
       * A zero ends the current nonzero run.
       *
       * Store its length in the reserved
       * code position, then reserve another
       * position for the next run.
       *
       * The original zero is not copied.
       * The decoder reconstructs it later.
       */
      output[code_index] = code;

      if (write_index >= output_capacity) {
        return false;
      }

      code_index = write_index;
      ++write_index;
      code = 1U;
    } else {
      /*
       * Copy a nonzero byte directly.
       *
       * The encoded body must not contain
       * any zero values, so these bytes
       * do not need transformation.
       */
      if (write_index >= output_capacity) {
        return false;
      }

      output[write_index] = input[i];
      ++write_index;
      ++code;
    }
  }

  /*
   * Complete the final run.
   *
   * This is necessary even when the input
   * ends with zero or contains no bytes.
   *
   * For example:
   *   Raw:     11 00
   *   Encoded: 02 11 01
   */
  output[code_index] = code;

  *output_length = write_index;
  return true;
}

/*
 * Decode a zero-free COBS body.
 *
 * The caller must remove the frame delimiter
 * before calling this function.
 *
 * No heap storage is used, and output is limited
 * to the protocol's maximum 32 decoded bytes.
 */
bool cobs_decode(const uint8_t *input, size_t input_length, uint8_t *output,
                 size_t output_capacity, size_t *output_length) {
  if (output_length == NULL) {
    return false;
  }

  *output_length = 0U;

  /*
   * Empty input is not valid COBS encoding.
   *
   * A zero-length decoded packet is encoded
   * as a single byte containing 0x01.
   */
  if (input == NULL || output == NULL || input_length == 0U ||
      input_length > COBS_MAX_ENCODED_SIZE) {
    return false;
  }

  size_t read_index = 0U;
  size_t write_index = 0U;

  while (read_index < input_length) {
    /*
     * The first byte in each COBS block
     * specifies the block length.
     *
     * A zero code is invalid because zero
     * is reserved as the frame delimiter.
     */
    const uint8_t code = input[read_index];

    if (code == 0U) {
      return false;
    }

    ++read_index;

    /*
     * A code N requires N-1 literal bytes.
     *
     * Reject codes that refer beyond the
     * encoded body, preventing out-of-bounds
     * reads on corrupted RS-485 traffic.
     */
    const size_t count = (size_t)code - 1U;

    if (count > input_length - read_index) {
      return false;
    }

    /*
     * Reject blocks that cannot fit in
     * either the caller's buffer or the
     * protocol's maximum decoded size.
     */
    if (count >
        output_capacity -
            (write_index <= output_capacity ? write_index : output_capacity)) {
      return false;
    }

    if (count > COBS_MAX_DECODED_SIZE - write_index) {
      return false;
    }

    /*
     * Copy the nonzero run.
     *
     * A literal zero inside a block indicates
     * malformed COBS input and must be rejected.
     */
    for (size_t i = 0U; i < count; ++i) {
      const uint8_t value = input[read_index];

      if (value == 0U) {
        return false;
      }

      output[write_index] = value;

      ++read_index;
      ++write_index;
    }

    /*
     * A code below 255 normally represents
     * a run terminated by an original zero.
     *
     * Restore that zero only if another
     * encoded block follows.
     *
     * Without this condition we would add
     * an unwanted zero to the packet end.
     */
    if (code != UINT8_MAX && read_index < input_length) {

      if (write_index >= output_capacity ||
          write_index >= COBS_MAX_DECODED_SIZE) {
        return false;
      }

      output[write_index] = 0U;
      ++write_index;
    }
  }

  *output_length = write_index;
  return true;
}
