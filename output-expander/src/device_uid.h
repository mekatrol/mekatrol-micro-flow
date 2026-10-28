
#ifndef OUTPUT_EXPANDER_DEVICE_UID_H
#define OUTPUT_EXPANDER_DEVICE_UID_H

#include "dispatcher.h"

#include <stdint.h>

/*
 * The CH32V003 electronic signature exposes three 32-bit words.
 *
 * Protocol v1 represents the UID as 12 consecutive raw bytes.
 * Each word is serialized least-significant byte first and the
 * three words appear in increasing register-address order.
 *
 * The host's UID hexadecimal string must use this same order.
 */
#define DEVICE_UID_SIZE DISPATCHER_UID_SIZE

/*
 * Convert three UID words to the protocol's fixed byte order.
 *
 * This hardware-independent helper is separately host-testable.
 * The caller supplies storage for exactly DEVICE_UID_SIZE bytes.
 */
void device_uid_from_words(uint32_t word0, uint32_t word1, uint32_t word2,
                           uint8_t uid[DEVICE_UID_SIZE]);

/*
 * Read the MCU's factory-programmed electronic signature.
 *
 * The signature is read-only and needs no peripheral clock,
 * GPIO configuration, interrupt or EEPROM transaction.
 *
 * Call from foreground code during initialization.
 */
void device_uid_read(uint8_t uid[DEVICE_UID_SIZE]);

#endif
