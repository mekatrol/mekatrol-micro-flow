#include "crc16.h"

#include <stddef.h>
#include <stdint.h>

/*
 * CRC-16/CCITT-FALSE polynomial.
 *
 * The polynomial determines how bits are mixed
 * into the checksum during processing.
 *
 * This must match the Python host implementation.
 */
#define CRC16_POLYNOMIAL UINT16_C(0x1021)
#define CRC16_INITIAL UINT16_C(0xFFFF)

uint16_t crc16_ccitt_false(const uint8_t *data, size_t length) {
  /*
   * CCITT-FALSE starts with every CRC bit set.
   *
   * Using a different initial value would
   * produce incompatible checksums even if
   * the polynomial were identical.
   */
  uint16_t crc = CRC16_INITIAL;

  /*
   * Process each input byte in transmission order.
   *
   * The protocol calculates the CRC over VERSION,
   * DEST, SEQ, TYPE, CMD, LEN and PAYLOAD.
   *
   * The two CRC bytes themselves are excluded.
   */
  for (size_t i = 0U; i < length; ++i) {

    /*
     * Place the incoming byte into the upper
     * eight bits of the CRC accumulator.
     *
     * CCITT-FALSE processes the most significant
     * bit first and does not reflect the input.
     */
    crc ^= (uint16_t)((uint16_t)data[i] << 8);

    /*
     * Process all eight bits of this byte.
     *
     * If the highest CRC bit is set, shifting
     * it out requires applying the polynomial.
     *
     * Otherwise, only shift the accumulator.
     */
    for (uint8_t bit = 0U; bit < 8U; ++bit) {

      if ((crc & UINT16_C(0x8000)) != 0U) {

        crc = (uint16_t)((uint16_t)(crc << 1) ^ CRC16_POLYNOMIAL);

      } else {

        crc = (uint16_t)(crc << 1);
      }
    }
  }

  /*
   * No final XOR is applied for CCITT-FALSE.
   *
   * The packet encoder will separately convert
   * this value into little-endian wire bytes.
   */
  return crc;
}
