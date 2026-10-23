#include "rp2040_usb_cdc_sdk.h"

#include "tusb.h"

static microflow_rp2040_usb_cdc_t *active_cdc;

static size_t ring_count(size_t read, size_t write, size_t capacity) {
  return write >= read ? write - read : capacity - read + write;
}

static bool ring_push(uint8_t *storage, size_t capacity, size_t *read,
                      size_t *write, uint8_t value) {
  const size_t next = (*write + 1U) % capacity;
  if (next == *read) return false;
  storage[*write] = value;
  *write = next;
  return true;
}

bool microflow_rp2040_usb_cdc_initialize(microflow_rp2040_usb_cdc_t *cdc,
                                         uint8_t *receive_storage,
                                         size_t receive_capacity,
                                         uint8_t *transmit_storage,
                                         size_t transmit_capacity) {
  if ((cdc == NULL) || (receive_storage == NULL) || (receive_capacity < 2U) ||
      (transmit_storage == NULL) || (transmit_capacity < 2U)) return false;
  *cdc = (microflow_rp2040_usb_cdc_t){
      .receive_storage = receive_storage,
      .receive_capacity = receive_capacity,
      .transmit_storage = transmit_storage,
      .transmit_capacity = transmit_capacity};
  active_cdc = cdc;
  return tusb_init();
}

void microflow_rp2040_usb_cdc_task(microflow_rp2040_usb_cdc_t *cdc) {
  tud_task();
  cdc->configured = tud_cdc_connected();

  while (tud_cdc_available() != 0U) {
    const uint8_t value = (uint8_t)tud_cdc_read_char();
    if (!ring_push(cdc->receive_storage, cdc->receive_capacity,
                   &cdc->receive_read, &cdc->receive_write, value)) {
      ++cdc->receive_dropped;
    } else {
      const size_t count = ring_count(cdc->receive_read, cdc->receive_write,
                                      cdc->receive_capacity);
      if (count > cdc->receive_high_water) cdc->receive_high_water = (uint32_t)count;
    }
  }

  while ((cdc->transmit_read != cdc->transmit_write) &&
         (tud_cdc_write_available() != 0U)) {
    tud_cdc_write_char((char)cdc->transmit_storage[cdc->transmit_read]);
    cdc->transmit_read = (cdc->transmit_read + 1U) % cdc->transmit_capacity;
  }
  tud_cdc_write_flush();
}

bool microflow_rp2040_usb_cdc_read(microflow_rp2040_usb_cdc_t *cdc,
                                   uint8_t *value) {
  if ((value == NULL) || (cdc->receive_read == cdc->receive_write)) return false;
  *value = cdc->receive_storage[cdc->receive_read];
  cdc->receive_read = (cdc->receive_read + 1U) % cdc->receive_capacity;
  return true;
}

size_t microflow_rp2040_usb_cdc_write(microflow_rp2040_usb_cdc_t *cdc,
                                      const uint8_t *data, size_t length) {
  size_t written = 0U;
  while ((written < length) && ring_push(cdc->transmit_storage,
          cdc->transmit_capacity, &cdc->transmit_read, &cdc->transmit_write,
          data[written])) ++written;
  cdc->transmit_dropped += (uint32_t)(length - written);
  const size_t count = ring_count(cdc->transmit_read, cdc->transmit_write,
                                  cdc->transmit_capacity);
  if (count > cdc->transmit_high_water) cdc->transmit_high_water = (uint32_t)count;
  return written;
}

void tud_mount_cb(void) {
  if (active_cdc != NULL) active_cdc->configured = true;
}

void tud_umount_cb(void) {
  if (active_cdc != NULL) {
    active_cdc->configured = false;
    ++active_cdc->resets;
  }
}
