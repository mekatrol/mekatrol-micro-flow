/* Polling TinyUSB CDC bridge with deterministic caller-provided ring storage. */
#include "rp2040_usb_cdc_sdk.h"

#include "tusb.h"

/* TinyUSB callbacks refer to the single statically configured CDC instance. */
static microflow_rp2040_usb_cdc_t *active_cdc;

/* Calculate queued occupancy for high-water diagnostics without extra state. */
static size_t ring_count(size_t read, size_t write, size_t capacity) {
    /* Modular distance relies on retaining one unused sentinel slot. */
    return write >= read ? write - read : capacity - read + write;
}

/* Append to a caller-owned SPSC-style ring while preserving unread bytes. */
static bool ring_push(uint8_t *storage, size_t capacity, size_t *read,
                      size_t *write, uint8_t value) {
    /* Reject newest on full so unread data and FIFO ordering are preserved. */
    const size_t next = (*write + 1U) % capacity;
    if (next == *read) {
        return false;
    }
    storage[*write] = value;
    *write          = next;
    return true;
}

/* Validate and bind fixed RX/TX buffers before starting the TinyUSB device. */
bool microflow_rp2040_usb_cdc_initialize(microflow_rp2040_usb_cdc_t *cdc,
                                         uint8_t                    *receive_storage,
                                         size_t                      receive_capacity,
                                         uint8_t                    *transmit_storage,
                                         size_t                      transmit_capacity) {
    /* Missing buffers and capacities below two cannot form useful rings. */
    if ((cdc == NULL) || (receive_storage == NULL) || (receive_capacity < 2U) ||
        (transmit_storage == NULL) || (transmit_capacity < 2U)) {
        return false;
    }
    *cdc = (microflow_rp2040_usb_cdc_t){
        .receive_storage   = receive_storage,
        .receive_capacity  = receive_capacity,
        .transmit_storage  = transmit_storage,
        .transmit_capacity = transmit_capacity
    };
    /* Register only after complete state construction for callback safety. */
    active_cdc = cdc;
    return tusb_init();
}

/* Poll USB and bridge available CDC bytes to and from bounded application rings. */
void microflow_rp2040_usb_cdc_task(microflow_rp2040_usb_cdc_t *cdc) {
    /* TinyUSB progresses only when polled in this cooperative configuration. */
    tud_task();
    cdc->configured = tud_cdc_connected();

    while (tud_cdc_available() != 0U) {
        /* Drain the device FIFO even when the bounded application ring is full. */
        const uint8_t value = (uint8_t)tud_cdc_read_char();
        if (!ring_push(cdc->receive_storage, cdc->receive_capacity,
                       &cdc->receive_read, &cdc->receive_write, value)) {
            ++cdc->receive_dropped;
        } else {
            const size_t count = ring_count(cdc->receive_read, cdc->receive_write,
                                            cdc->receive_capacity);
            if (count > cdc->receive_high_water) {
                cdc->receive_high_water = (uint32_t)count;
            }
        }
    }

    while ((cdc->transmit_read != cdc->transmit_write) &&
           (tud_cdc_write_available() != 0U)) {
        /* Preserve unsent bytes for a later poll when TinyUSB has no more space. */
        tud_cdc_write_char((char)cdc->transmit_storage[cdc->transmit_read]);
        cdc->transmit_read = (cdc->transmit_read + 1U) % cdc->transmit_capacity;
    }
    tud_cdc_write_flush();
}

/* Remove one received byte without blocking the cooperative application loop. */
bool microflow_rp2040_usb_cdc_read(microflow_rp2040_usb_cdc_t *cdc,
                                   uint8_t                    *value) {
    /* Reads are deliberately non-blocking for the cooperative main loop. */
    if ((value == NULL) || (cdc->receive_read == cdc->receive_write)) {
        return false;
    }
    *value            = cdc->receive_storage[cdc->receive_read];
    cdc->receive_read = (cdc->receive_read + 1U) % cdc->receive_capacity;
    return true;
}

/* Queue the fitting prefix of an application message and account for overflow. */
size_t microflow_rp2040_usb_cdc_write(microflow_rp2040_usb_cdc_t *cdc,
                                      const uint8_t *data, size_t length) {
    /* Partial acceptance is visible in both the return value and drop metric. */
    size_t written = 0U;
    while ((written < length) && ring_push(cdc->transmit_storage,
                                           cdc->transmit_capacity, &cdc->transmit_read, &cdc->transmit_write,
                                           data[written])) {
        ++written;
    }
    cdc->transmit_dropped += (uint32_t)(length - written);
    const size_t count = ring_count(cdc->transmit_read, cdc->transmit_write,
                                    cdc->transmit_capacity);
    if (count > cdc->transmit_high_water) {
        cdc->transmit_high_water = (uint32_t)count;
    }
    return written;
}

/* Record successful host enumeration without performing work in the callback. */
void tud_mount_cb(void) {
    /* Enumeration state changes are recorded without doing application work. */
    if (active_cdc != NULL) {
        active_cdc->configured = true;
    }
}

/* Record disconnect state and count resets for operator-visible diagnostics. */
void tud_umount_cb(void) {
    /* Count disconnects so field diagnostics can identify unstable links. */
    if (active_cdc != NULL) {
        active_cdc->configured = false;
        ++active_cdc->resets;
    }
}
