
/*
 * Fixed-capacity single-producer, single-consumer byte ring.
 *
 * Storage is caller-owned. No dynamic allocation is performed.
 * A full ring rejects the newest byte without overwriting unread data.
 */
#include <microflow/byte_ring.h>

/* Advance one slot, wrapping at the fixed ring capacity. */
static uint16_t byte_next(uint16_t index) {
    return (uint16_t)((index + 1U) % MICROFLOW_BYTE_RING_CAPACITY);
}

/* Reset the ring and its diagnostic counters. */
void microflow_byte_ring_initialize(microflow_byte_ring_t *ring) {
    if (ring == NULL) {
        return;
    }

    ring->read_index  = 0U;
    ring->write_index = 0U;
    ring->high_water  = 0U;
    ring->rejected    = 0U;
}

/* Return the number of unread bytes. */
size_t microflow_byte_ring_count(const microflow_byte_ring_t *ring) {
    if (ring == NULL) {
        return 0U;
    }

    return (ring->write_index + MICROFLOW_BYTE_RING_CAPACITY -
            ring->read_index) %
           MICROFLOW_BYTE_RING_CAPACITY;
}

/*
 * Add one byte from the sole producer.
 *
 * One slot remains unused to distinguish a full ring from an empty ring.
 */
bool microflow_byte_ring_push_from_isr(microflow_byte_ring_t *ring,
                                       uint8_t                value) {
    if (ring == NULL) {
        return false;
    }

    const uint16_t next = byte_next(ring->write_index);

    if (next == ring->read_index) {
        ++ring->rejected;
        return false;
    }

    /* Write the byte before publishing the new producer index. */
    ring->storage[ring->write_index] = value;
    ring->write_index                = next;

    const size_t count = microflow_byte_ring_count(ring);

    if (count > ring->high_water) {
        ring->high_water = (uint16_t)count;
    }

    return true;
}

/* Remove the oldest byte for the sole consumer. */
bool microflow_byte_ring_pop(microflow_byte_ring_t *ring,
                             uint8_t               *value_out) {
    if ((ring == NULL) || (value_out == NULL) ||
        (ring->read_index == ring->write_index)) {
        return false;
    }

    *value_out       = ring->storage[ring->read_index];
    ring->read_index = byte_next(ring->read_index);

    return true;
}
