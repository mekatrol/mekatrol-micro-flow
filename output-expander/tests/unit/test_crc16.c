
#include "crc16.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_reference_vector(void) {
  /*
   * Standard CRC-16/CCITT-FALSE test vector.
   *
   * The protocol implementation must produce 0x29B1.
   */
  static const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

  assert(crc16_ccitt_false(data, sizeof(data)) == UINT16_C(0x29B1));
}

static void test_empty_input(void) {
  /*
   * An empty input leaves the CRC accumulator
   * at its initial value of 0xFFFF.
   */
  assert(crc16_ccitt_false(NULL, 0U) == UINT16_C(0xFFFF));
}

static void test_zero_bytes(void) {
  /*
   * Protocol data may contain zero bytes.
   * CRC calculation must process them normally.
   *
   * COBS encoding happens separately, after
   * the CRC has been appended to the packet.
   */
  static const uint8_t data[] = {0x00U, 0x00U, 0x00U};

  assert(crc16_ccitt_false(data, sizeof(data)) == UINT16_C(0xCC9C));
}

static void test_single_bit_corruption(void) {
  /*
   * A single-bit change must alter the CRC.
   *
   * This checks a basic corruption-detection
   * property without involving UART hardware.
   */
  static const uint8_t original[] = {0x01U, 0x07U, 0x09U};

  static const uint8_t corrupted[] = {0x01U, 0x07U, 0x08U};

  assert(crc16_ccitt_false(original, sizeof(original)) !=
         crc16_ccitt_false(corrupted, sizeof(corrupted)));
}

int main(void) {
  test_reference_vector();
  test_empty_input();
  test_zero_bytes();
  test_single_bit_corruption();

  puts("All CRC-16 tests passed.");

  return 0;
}
