#ifndef MICROFLOW_RASPBERRY_PI_PICO_UPLOAD_TRANSPORT_H
#define MICROFLOW_RASPBERRY_PI_PICO_UPLOAD_TRANSPORT_H

#include "supervisor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MICROFLOW_UPLOAD_PROTOCOL_VERSION 1U
#define MICROFLOW_UPLOAD_MAX_FRAME_PAYLOAD 260U
#define MICROFLOW_UPLOAD_MAX_IMAGE_SIZE 2048U
#define MICROFLOW_UPLOAD_FRAME_OVERHEAD 16U

typedef enum {
    MICROFLOW_UPLOAD_PROBE = 1U,
    MICROFLOW_UPLOAD_DEVICE_INFO = 2U,
    MICROFLOW_UPLOAD_BEGIN = 3U,
    MICROFLOW_UPLOAD_DATA = 4U,
    MICROFLOW_UPLOAD_COMPLETE = 5U,
    MICROFLOW_UPLOAD_ABORT = 6U,
    MICROFLOW_UPLOAD_VALIDATION_RESULT = 7U,
    MICROFLOW_UPLOAD_ACTIVATION_RESULT = 8U,
    MICROFLOW_UPLOAD_ERROR = 127U
} microflow_upload_message_type_t;

typedef enum {
    MICROFLOW_UPLOAD_OK = 0U,
    MICROFLOW_UPLOAD_ERROR_MAGIC = 1U,
    MICROFLOW_UPLOAD_ERROR_VERSION = 2U,
    MICROFLOW_UPLOAD_ERROR_TYPE = 3U,
    MICROFLOW_UPLOAD_ERROR_LENGTH = 4U,
    MICROFLOW_UPLOAD_ERROR_SEQUENCE = 5U,
    MICROFLOW_UPLOAD_ERROR_CHECKSUM = 6U,
    MICROFLOW_UPLOAD_ERROR_STATE = 7U,
    MICROFLOW_UPLOAD_ERROR_OFFSET = 8U,
    MICROFLOW_UPLOAD_ERROR_IMAGE_CHECKSUM = 9U,
    MICROFLOW_UPLOAD_ERROR_VALIDATION = 10U,
    MICROFLOW_UPLOAD_ERROR_TX_OVERFLOW = 11U
} microflow_upload_result_t;

typedef bool (*microflow_upload_validate_t)(void *context, const uint8_t *image,
                                             size_t length);
typedef bool (*microflow_upload_activate_t)(void *context, const uint8_t *image,
                                             size_t length);

typedef struct {
    microflow_byte_ring_t *receive;
    microflow_byte_ring_t *transmit;
    microflow_upload_validate_t validate;
    microflow_upload_activate_t activate;
    void *callback_context;
    uint8_t staging[MICROFLOW_UPLOAD_MAX_IMAGE_SIZE];
    uint8_t frame_payload[MICROFLOW_UPLOAD_MAX_FRAME_PAYLOAD];
    uint8_t header[MICROFLOW_UPLOAD_FRAME_OVERHEAD];
    uint32_t expected_sequence, image_length, image_checksum, received_length;
    uint32_t frames_received, frames_rejected, uploads_completed, uploads_aborted;
    uint16_t header_used, payload_used;
    uint32_t last_receive_tick;
    bool upload_active, receive_tick_valid;
} microflow_upload_transport_t;

void microflow_upload_transport_initialize(microflow_upload_transport_t *transport,
    microflow_byte_ring_t *receive, microflow_byte_ring_t *transmit,
    microflow_upload_validate_t validate, microflow_upload_activate_t activate,
    void *callback_context);
void microflow_upload_transport_service(microflow_upload_transport_t *transport,
                                        uint32_t now);
bool microflow_upload_transport_expire(microflow_upload_transport_t *transport,
                                       uint32_t now, uint32_t timeout);
void microflow_upload_transport_erase_staging(microflow_upload_transport_t *transport);
uint32_t microflow_upload_crc32(const uint8_t *data, size_t length);

#endif
