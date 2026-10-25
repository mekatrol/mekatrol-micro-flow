/* Private state and module contracts for the Pico firmware application. */
#ifndef MICROFLOW_PICO_FIRMWARE_INTERNAL_H
#define MICROFLOW_PICO_FIRMWARE_INTERNAL_H

#include <microflow/console.h>
#include <microflow/flow_store.h>
#include <microflow/supervisor.h>
#include <microflow/upload_transport.h>

#include "board_gpio.h"
#include "firmware_config.h"
#include "rp2040_usb_cdc_sdk.h"

typedef struct {
    microflow_supervisor_t       supervisor;
    microflow_byte_ring_t        uart_receive;
    microflow_byte_ring_t        uart_transmit;
    microflow_upload_transport_t upload;
    uint8_t                      active_flow[MICROFLOW_UPLOAD_MAX_IMAGE_SIZE];
    microflow_flow_store_t       flow_store;
    microflow_rp2040_usb_cdc_t   usb;
    uint8_t                      usb_receive[MICROFLOW_USB_QUEUE_CAPACITY];
    uint8_t                      usb_transmit[MICROFLOW_USB_QUEUE_CAPACITY];
    microflow_byte_ring_t        console_receive;
    microflow_console_t          console;
    microflow_board_gpio_t       gpio;
} microflow_firmware_t;

void microflow_firmware_console_initialize(microflow_firmware_t *firmware);
void microflow_firmware_usb_service(void *context, uint32_t events);
void microflow_firmware_usb_write(microflow_firmware_t *firmware,
                                  const uint8_t *data, size_t length);
void microflow_firmware_usb_write_text(microflow_firmware_t *firmware,
                                       const char           *text);
void microflow_firmware_usb_write_u32(microflow_firmware_t *firmware,
                                      uint32_t              value);
void microflow_firmware_status_service(void *context, uint32_t events);
void microflow_firmware_uart_service(void *context, uint32_t events);
microflow_supervisor_platform_t
         microflow_firmware_platform(microflow_firmware_t *firmware);
uint32_t microflow_firmware_lock(void *context);
void     microflow_firmware_unlock(void *context, uint32_t state);
void     microflow_firmware_flow_stopped(void *context);

#endif
