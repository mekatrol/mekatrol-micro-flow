
/* Private state and module contracts for the Pico firmware application. */
#ifndef MICROFLOW_PICO_FIRMWARE_INTERNAL_H
#define MICROFLOW_PICO_FIRMWARE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include <microflow/byte_ring.h>
#include <microflow/console.h>
#include <microflow/flow_store.h>
#include <microflow/upload_transport.h>

#include "board_gpio.h"
#include "firmware_config.h"
#include "rp2040_usb_cdc_sdk.h"

/*
 * FreeRTOS task stack sizes are measured in StackType_t elements,
 * not bytes.
 *
 * Verify stack high-water marks on hardware before finalizing
 * the production RAM budget.
 */
#define MICROFLOW_USB_TASK_STACK_WORDS    1024U
#define MICROFLOW_UART_TASK_STACK_WORDS   512U
#define MICROFLOW_STATUS_TASK_STACK_WORDS 512U

typedef struct {
    /* Application data and transports. */
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

    /* FreeRTOS task handles. */
    TaskHandle_t usb_task;
    TaskHandle_t uart_task;
    TaskHandle_t status_task;

    /* Statically allocated FreeRTOS task control blocks. */
    StaticTask_t usb_task_tcb;
    StaticTask_t uart_task_tcb;
    StaticTask_t status_task_tcb;

    /* Statically allocated task stacks. */
    StackType_t usb_task_stack[MICROFLOW_USB_TASK_STACK_WORDS];
    StackType_t uart_task_stack[MICROFLOW_UART_TASK_STACK_WORDS];
    StackType_t status_task_stack[MICROFLOW_STATUS_TASK_STACK_WORDS];
} microflow_firmware_t;

void microflow_firmware_console_initialize(microflow_firmware_t *firmware);

/* USB CDC and console services. */
void microflow_firmware_usb_service(void *context);
void microflow_firmware_usb_write(microflow_firmware_t *firmware,
                                  const uint8_t *data, size_t length);
void microflow_firmware_usb_write_text(microflow_firmware_t *firmware,
                                       const char           *text);
void microflow_firmware_usb_write_u32(microflow_firmware_t *firmware,
                                      uint32_t              value);

/* UART and periodic status services. */
void microflow_firmware_status_service(void *context);
void microflow_firmware_uart_service(void *context);

/* Flow store synchronization and safety callbacks. */
uint32_t microflow_firmware_lock(void *context);
void     microflow_firmware_unlock(void *context, uint32_t state);
void     microflow_firmware_flow_stopped(void *context);

#endif /* MICROFLOW_PICO_FIRMWARE_INTERNAL_H */
