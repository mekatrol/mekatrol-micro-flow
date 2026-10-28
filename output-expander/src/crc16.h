
#ifndef OUTPUT_EXPANDER_CRC16_H
#define OUTPUT_EXPANDER_CRC16_H

#include <stddef.h>
#include <stdint.h>

/*
 * Calculate CRC-16/CCITT-FALSE.
 *
 * WHAT:
 * Produces a 16-bit checksum for an array of bytes.
 *
 * WHY:
 * RS-485 communication may experience electrical
 * noise or corrupted characters. The CRC detects
 * accidental changes to a protocol frame.
 *
 * Parameters:
 *   Polynomial:  0x1021
 *   Initial CRC: 0xFFFF
 *   Reflected:   No
 *   Final XOR:   0x0000
 *
 * The protocol stores the resulting CRC in
 * little-endian order (low byte first).
 *
 * CRC detects corruption but does not provide
 * authentication or encryption.
 */
uint16_t crc16_ccitt_false(const uint8_t *data, size_t length);

#endif
