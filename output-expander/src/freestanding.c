/*
 * Minimal C memory functions for the freestanding CH32V003 firmware.
 *
 * WHY:
 * GCC may generate memcpy() or memset() when copying or
 * initializing structures, even if our source code does not
 * explicitly call those functions.
 *
 * The firmware links with -nostdlib, so these functions are
 * not supplied automatically by the C standard library.
 *
 * No heap allocation or hardware registers are involved.
 */

#include <stddef.h>

/*
 * string.h declares the standard memcpy() and memset()
 * function prototypes.
 *
 * WHY:
 * -Wmissing-prototypes requires a declaration before
 * defining any globally visible function.
 *
 * Including string.h does NOT link the C standard library.
 * Our implementations below still supply the actual
 * functions required by the bare-metal firmware.
 */
#include <string.h>

/*
 * Copy count bytes from source to destination.
 *
 * IMPORTANT:
 * Like standard memcpy(), the source and destination regions
 * must not overlap. Use memmove() for overlapping regions.
 */
void *memcpy(void *restrict destination, const void *restrict source,
             size_t count) {
  unsigned char *dst = destination;
  const unsigned char *src = source;

  for (size_t i = 0U; i < count; ++i) {
    dst[i] = src[i];
  }

  return destination;
}

/*
 * Fill count bytes with the low eight bits of value.
 *
 * An unsigned char is used because C defines object
 * representations in terms of character-sized bytes.
 */
void *memset(void *destination, int value, size_t count) {
  unsigned char *dst = destination;
  const unsigned char byte = (unsigned char)value;

  for (size_t i = 0U; i < count; ++i) {
    dst[i] = byte;
  }

  return destination;
}