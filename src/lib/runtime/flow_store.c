/* Portable active flow image storage and lifecycle implementation. */
#include <microflow/flow.h>
#include <microflow/flow_store.h>

void microflow_flow_store_initialize(
    microflow_flow_store_t *store, uint8_t *storage, size_t capacity,
    microflow_flow_store_lock_t lock, microflow_flow_store_unlock_t unlock,
    microflow_flow_store_stopped_t stopped, void *context) {
    if (store == NULL) {
        return;
    }
    *store = (microflow_flow_store_t){
        .storage = storage, .capacity = capacity, .lock = lock, .unlock = unlock, .stopped = stopped, .context = context
    };
}

bool microflow_flow_store_activate(void *context, const uint8_t *image,
                                   size_t length) {
    microflow_flow_store_t *store = context;
    if ((store == NULL) || (store->storage == NULL) || (image == NULL) ||
        (length > store->capacity) ||
        ((store->lock == NULL) != (store->unlock == NULL))) {
        return false;
    }
    uint32_t state = 0U;
    if (store->lock != NULL) {
        state = store->lock(store->context);
    }
    for (size_t i = 0U; i < length; ++i) {
        store->storage[i] = image[i];
    }
    store->length  = length;
    store->running = false;
    if (store->unlock != NULL) {
        store->unlock(store->context, state);
    }
    return true;
}

bool microflow_flow_store_validate(const microflow_flow_store_t *store) {
    return (store != NULL) && (store->length != 0U) &&
           microflow_flow_validate(NULL, store->storage, store->length);
}

bool microflow_flow_store_start(microflow_flow_store_t *store) {
    if (!microflow_flow_store_validate(store)) {
        return false;
    }
    store->running = true;
    return true;
}

void microflow_flow_store_stop(microflow_flow_store_t *store) {
    if (store == NULL) {
        return;
    }
    store->running = false;
    if (store->stopped != NULL) {
        store->stopped(store->context);
    }
}
