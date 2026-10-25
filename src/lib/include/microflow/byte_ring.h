
#ifndef MICROFLOW_BYTE_RING_H
#define MICROFLOW_BYTE_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MICROFLOW_BYTE_RING_CAPACITY 128U

typedef struct {
    uint8_t           storage[MICROFLOW_BYTE_RING_CAPACITY];
    volatile uint16_t read_index;
    volatile uint16_t write_index;
    uint16_t          high_water;
    uint32_t          rejected;
} microflow_byte_ring_t;

void microflow_byte_ring_initialize(microflow_byte_ring_t *ring);

size_t microflow_byte_ring_count(const microflow_byte_ring_t *ring);

bool microflow_byte_ring_push_from_isr(microflow_byte_ring_t *ring,
                                       uint8_t                value);

bool microflow_byte_ring_pop(microflow_byte_ring_t *ring,
                             uint8_t               *value_out);

#endif /* MICROFLOW_BYTE_RING_H */
