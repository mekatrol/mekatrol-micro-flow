/* Portable, caller-owned active flow image lifecycle. */
#ifndef MICROFLOW_FLOW_STORE_H
#define MICROFLOW_FLOW_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t (*microflow_flow_store_lock_t)(void *context);
typedef void (*microflow_flow_store_unlock_t)(void *context, uint32_t state);
typedef void (*microflow_flow_store_stopped_t)(void *context);

typedef struct {
    uint8_t                       *storage;
    size_t                         capacity;
    volatile size_t                length;
    bool                           running;
    microflow_flow_store_lock_t    lock;
    microflow_flow_store_unlock_t  unlock;
    microflow_flow_store_stopped_t stopped;
    void                          *context;
} microflow_flow_store_t;

void microflow_flow_store_initialize(
    microflow_flow_store_t *store, uint8_t *storage, size_t capacity,
    microflow_flow_store_lock_t lock, microflow_flow_store_unlock_t unlock,
    microflow_flow_store_stopped_t stopped, void *context);
bool microflow_flow_store_activate(void *context, const uint8_t *image,
                                   size_t length);
bool microflow_flow_store_validate(const microflow_flow_store_t *store);
bool microflow_flow_store_start(microflow_flow_store_t *store);
void microflow_flow_store_stop(microflow_flow_store_t *store);

#endif
