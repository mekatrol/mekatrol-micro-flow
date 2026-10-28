
#include "device_uid.h"

#include <stddef.h>
#include <stdint.h>

/*
 * CH32V003 electronic-signature registers.
 *
 * These addresses are specified by the CH32V003 reference manual:
 *
 * 0x1FFFF7E8: UID bits  0..31
 * 0x1FFFF7EC: UID bits 32..63
 * 0x1FFFF7F0: UID bits 64..95
 *
 * Volatile requires an actual read of each memory-mapped register.
 * Reading them does not modify the electronic signature.
 */
#define DEVICE_UID_REG0                                                        \
  (*(volatile const uint32_t *)(uintptr_t)UINT32_C(0x1FFFF7E8))
#define DEVICE_UID_REG1                                                        \
  (*(volatile const uint32_t *)(uintptr_t)UINT32_C(0x1FFFF7EC))
#define DEVICE_UID_REG2                                                        \
  (*(volatile const uint32_t *)(uintptr_t)UINT32_C(0x1FFFF7F0))

_Static_assert(DEVICE_UID_SIZE == 12U, "Expected 96-bit UID");

void device_uid_from_words(uint32_t word0, uint32_t word1, uint32_t word2,
                           uint8_t uid[DEVICE_UID_SIZE]) {
  if (uid == NULL) {
    return;
  }

  /*
   * Explicit shifts avoid relying on the C compiler's structure layout,
   * pointer aliasing rules or host-machine byte order.
   *
   * Each cast intentionally retains the selected low eight bits.
   */
  const uint32_t words[3] = {word0, word1, word2};

  for (size_t word = 0U; word < 3U; ++word) {
    for (size_t byte = 0U; byte < 4U; ++byte) {
      uid[word * 4U + byte] = (uint8_t)(words[word] >> (byte * 8U));
    }
  }
}

void device_uid_read(uint8_t uid[DEVICE_UID_SIZE]) {
  if (uid == NULL) {
    return;
  }

  /*
   * Read each register once before serializing.
   *
   * The factory signature is immutable during normal operation.
   * No critical section or interrupt synchronization is needed.
   */
  const uint32_t word0 = DEVICE_UID_REG0;
  const uint32_t word1 = DEVICE_UID_REG1;
  const uint32_t word2 = DEVICE_UID_REG2;

  device_uid_from_words(word0, word1, word2, uid);
}
