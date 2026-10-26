
#ifndef RP2_WS2812_H
#define RP2_WS2812_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/pio.h"

/*
 * Represents one independently controlled WS2812 output.
 *
 * Each output owns one PIO state machine, but several outputs
 * may share the same PIO program in instruction memory.
 *
 * The caller must keep this structure alive while the driver
 * is using it.
 *
 * Calls are not thread-safe. The caller must serialize access to a
 * strip, and must serialize init/deinit calls across all strips.
 */
typedef struct {
    PIO  pio;         /* PIO block: pio0, pio1, etc. */
    uint sm;          /* State machine within that block */
    uint pin;         /* GPIO used for LED data */
    bool initialized; /* Whether this output is active */
} rp2_ws2812_strip_t;

/*
 * Initialize a WS2812 output.
 *
 * Automatically searches for an available PIO state machine.
 *
 * Returns false if the GPIO is invalid or no suitable PIO
 * resources are available.
 *
 * The strip structure must initially be zero-initialized.
 */
bool rp2_ws2812_init(rp2_ws2812_strip_t *strip, uint pin);

/*
 * Send one RGB colour.
 *
 * Red, green and blue are converted to WS2812 GRB order.
 *
 * This call blocks until the colour word is accepted into
 * the PIO TX FIFO. It does NOT necessarily mean that the
 * bits have finished transmitting.
 */
bool rp2_ws2812_write_rgb(
    rp2_ws2812_strip_t *strip,
    uint8_t             red,
    uint8_t             green,
    uint8_t             blue);

/*
 * Send a sequence of packed RGB colours.
 *
 * Each uint32_t contains 0x00RRGGBB.
 * The driver converts the colours to GRB automatically.
 *
 * All colour data is queued in order.
 */
bool rp2_ws2812_write_pixels(
    rp2_ws2812_strip_t *strip,
    const uint32_t     *pixels,
    size_t              count);

/*
 * Release the state machine owned by this strip.
 *
 * Call only when no transmission is pending.
 */
void rp2_ws2812_deinit(rp2_ws2812_strip_t *strip);

#endif /* RP2_WS2812_H */
