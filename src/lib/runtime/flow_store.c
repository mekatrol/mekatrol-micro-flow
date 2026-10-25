/* Portable active flow image storage and lifecycle implementation. */
#include <microflow/flow.h>
#include <microflow/flow_store.h>

/* Configure caller-owned storage and optional synchronization/lifecycle hooks. */
void microflow_flow_store_initialize(
    microflow_flow_store_t *store, uint8_t *storage, size_t capacity,
    microflow_flow_store_lock_t lock, microflow_flow_store_unlock_t unlock,
    microflow_flow_store_stopped_t stopped, void *context) {
    if (store == NULL) {
        /* Permit defensive initialization from composition code with no effects. */
        return;
    }
    /* Whole-object initialization also clears length and running state. */
    *store = (microflow_flow_store_t){
        .storage = storage, .capacity = capacity, .lock = lock, .unlock = unlock, .stopped = stopped, .context = context
    };
}

/* Atomically replace the active flow with a validated staged image. */
bool microflow_flow_store_activate(void *context, const uint8_t *image, size_t length) {
    microflow_flow_store_t *store = context;

    /* Lock/unlock are a pair: accepting only one would publish state unsafely. */
    if ((store == NULL) || (store->storage == NULL) || (image == NULL) ||
        (length > store->capacity) ||
        ((store->lock == NULL) != (store->unlock == NULL))) {
        return false;
    }

    uint32_t state = 0U;
    if (store->lock != NULL) {
        /* Preserve the platform's prior interrupt/critical-section state. */
        state = store->lock(store->context);
    }

    /* Fixed caller storage decouples execution from the mutable upload buffer. */
    for (size_t i = 0U; i < length; ++i) {
        store->storage[i] = image[i];
    }

    store->length = length;

    /* A replacement image must be explicitly started after activation. */
    store->running = false;
    if (store->unlock != NULL) {
        store->unlock(store->context, state);
    }
    
    return true;
}

/* Validate the complete active image envelope without changing lifecycle state. */
bool microflow_flow_store_validate(const microflow_flow_store_t *store) {
    return (store != NULL) && (store->length != 0U) &&
           microflow_flow_validate(NULL, store->storage, store->length);
}

/* Revalidate immediately before making the active image runnable. */
bool microflow_flow_store_start(microflow_flow_store_t *store) {
    if (!microflow_flow_store_validate(store)) {
        return false;
    }
    store->running = true;
    return true;
}

/* Leave running state and invoke the platform's output-safety hook. */
void microflow_flow_store_stop(microflow_flow_store_t *store) {
    if (store == NULL) {
        return;
    }
    store->running = false;
    if (store->stopped != NULL) {
        /* Boards use this hook to deassert outputs even after repeated stops. */
        store->stopped(store->context);
    }
}
