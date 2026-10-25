/* Bounded TinyUSB CDC adapter with caller-owned RX and TX storage. */
#ifndef MICROFLOW_RP2040_USB_CDC_SDK_H
#define MICROFLOW_RP2040_USB_CDC_SDK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Complete queue, metric, and connection state for one CDC interface. */
typedef struct {
  /* Receive ring is written by USB service and read by the application. */
  uint8_t *receive_storage;
  size_t receive_capacity;
  size_t receive_read;
  size_t receive_write;
  /* Transmit ring is written by the application and drained by USB service. */
  uint8_t *transmit_storage;
  size_t transmit_capacity;
  size_t transmit_read;
  size_t transmit_write;
  /* High-water and dropped counts expose bounded-capacity behavior. */
  uint32_t receive_high_water;
  uint32_t transmit_high_water;
  uint32_t receive_dropped;
  uint32_t transmit_dropped;
  /* Reset/configuration fields track host connection lifecycle. */
  uint32_t resets;
  bool configured;
} microflow_rp2040_usb_cdc_t;

/* Binds caller buffers; each ring needs at least two slots. */
bool microflow_rp2040_usb_cdc_initialize(microflow_rp2040_usb_cdc_t *cdc,
                                         uint8_t *receive_storage,
                                         size_t receive_capacity,
                                         uint8_t *transmit_storage,
                                         size_t transmit_capacity);
/* Polls TinyUSB, transfers bounded queued data, and tracks reconnect resets. */
void microflow_rp2040_usb_cdc_task(microflow_rp2040_usb_cdc_t *cdc);
/* Pops one received byte without blocking. */
bool microflow_rp2040_usb_cdc_read(microflow_rp2040_usb_cdc_t *cdc,
                                   uint8_t *value);
/* Queues as much of data as fits and returns the accepted byte count. */
size_t microflow_rp2040_usb_cdc_write(microflow_rp2040_usb_cdc_t *cdc,
                                      const uint8_t *data, size_t length);

#endif
