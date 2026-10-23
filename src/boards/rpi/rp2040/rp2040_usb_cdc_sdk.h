#ifndef MICROFLOW_RP2040_USB_CDC_SDK_H
#define MICROFLOW_RP2040_USB_CDC_SDK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t *receive_storage;
  size_t receive_capacity;
  size_t receive_read;
  size_t receive_write;
  uint8_t *transmit_storage;
  size_t transmit_capacity;
  size_t transmit_read;
  size_t transmit_write;
  uint32_t receive_high_water;
  uint32_t transmit_high_water;
  uint32_t receive_dropped;
  uint32_t transmit_dropped;
  uint32_t resets;
  bool configured;
} microflow_rp2040_usb_cdc_t;

bool microflow_rp2040_usb_cdc_initialize(microflow_rp2040_usb_cdc_t *cdc,
                                         uint8_t *receive_storage,
                                         size_t receive_capacity,
                                         uint8_t *transmit_storage,
                                         size_t transmit_capacity);
void microflow_rp2040_usb_cdc_task(microflow_rp2040_usb_cdc_t *cdc);
bool microflow_rp2040_usb_cdc_read(microflow_rp2040_usb_cdc_t *cdc,
                                   uint8_t *value);
size_t microflow_rp2040_usb_cdc_write(microflow_rp2040_usb_cdc_t *cdc,
                                      const uint8_t *data, size_t length);

#endif
