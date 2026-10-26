/*
 * Internal state for the minimal RP2040 FreeRTOS firmware.
 *
 * All task control blocks, stacks, and USB buffers are statically allocated.
 */
#ifndef MICROFLOW_PICO_FIRMWARE_INTERNAL_H
#define MICROFLOW_PICO_FIRMWARE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include <microflow/byte_ring.h>
#include <microflow/console.h>

#include "firmware_config.h"
#include "rp2040_usb_cdc_sdk.h"

#define MICROFLOW_USB_TASK_STACK_WORDS    1024U
#define MICROFLOW_STATUS_TASK_STACK_WORDS 512U

typedef struct {
    /* USB CDC adapter and its fixed receive/transmit storage. */
    microflow_rp2040_usb_cdc_t usb;

    uint8_t usb_receive[MICROFLOW_USB_QUEUE_CAPACITY];
    uint8_t usb_transmit[MICROFLOW_USB_QUEUE_CAPACITY];

    /* Portable console input storage and parser. */
    microflow_byte_ring_t console_receive;
    microflow_console_t   console;

    /* Statically allocated FreeRTOS task resources. */
    TaskHandle_t usb_task;
    TaskHandle_t status_task;

    StaticTask_t usb_task_tcb;
    StaticTask_t status_task_tcb;

    StackType_t usb_task_stack[MICROFLOW_USB_TASK_STACK_WORDS];
    StackType_t status_task_stack[MICROFLOW_STATUS_TASK_STACK_WORDS];
} microflow_firmware_t;

void microflow_firmware_console_initialize(
    microflow_firmware_t *firmware);

void microflow_firmware_usb_service(void *context);

void microflow_firmware_usb_write(
    microflow_firmware_t *firmware,
    const uint8_t        *data,
    size_t                length);

void microflow_firmware_usb_write_text(
    microflow_firmware_t *firmware,
    const char           *text);

void microflow_firmware_usb_write_u32(
    microflow_firmware_t *firmware,
    uint32_t              value);

#endif