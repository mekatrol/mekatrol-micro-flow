/* Bounded binary flow-image upload protocol over supervisor byte rings. */
#ifndef MICROFLOW_RASPBERRY_PI_PICO_UPLOAD_TRANSPORT_H
#define MICROFLOW_RASPBERRY_PI_PICO_UPLOAD_TRANSPORT_H

#include "supervisor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Wire-format revision accepted and emitted by this implementation. */
#define MICROFLOW_UPLOAD_PROTOCOL_VERSION 1U
/* Largest frame body, sized for a four-byte offset plus a 256-byte chunk. */
#define MICROFLOW_UPLOAD_MAX_FRAME_PAYLOAD 260U
/* Hard RAM and protocol bound for one staged flow image. */
#define MICROFLOW_UPLOAD_MAX_IMAGE_SIZE 2048U
/* Bytes occupied by magic, metadata, sequence, and checksum in each frame. */
#define MICROFLOW_UPLOAD_FRAME_OVERHEAD 16U

typedef enum {
    /* Host capability query; device answers with DEVICE_INFO. */
    MICROFLOW_UPLOAD_PROBE = 1U,
    /* Device response advertising protocol version and image capacity. */
    MICROFLOW_UPLOAD_DEVICE_INFO = 2U,
    /* Starts staging an image and declares its total length and CRC. */
    MICROFLOW_UPLOAD_BEGIN = 3U,
    /* Supplies a contiguous image chunk prefixed by its expected offset. */
    MICROFLOW_UPLOAD_DATA = 4U,
    /* Requests checksum validation and atomic activation of staged bytes. */
    MICROFLOW_UPLOAD_COMPLETE = 5U,
    /* Explicitly abandons the current staged transfer. */
    MICROFLOW_UPLOAD_ABORT = 6U,
    /* Reports whether the completed image passed semantic validation. */
    MICROFLOW_UPLOAD_VALIDATION_RESULT = 7U,
    /* Reports whether validated bytes became the active flow image. */
    MICROFLOW_UPLOAD_ACTIVATION_RESULT = 8U,
    /* Generic negative response carrying a microflow_upload_result_t. */
    MICROFLOW_UPLOAD_ERROR = 127U
} microflow_upload_message_type_t;

typedef enum {
    /* Request completed without a protocol or application error. */
    MICROFLOW_UPLOAD_OK = 0U,
    /* Reserved diagnostic for a frame without the "MFLO" sync marker. */
    MICROFLOW_UPLOAD_ERROR_MAGIC = 1U,
    /* Sender used an unsupported wire-format revision. */
    MICROFLOW_UPLOAD_ERROR_VERSION = 2U,
    /* Message type is unknown or invalid in this implementation. */
    MICROFLOW_UPLOAD_ERROR_TYPE = 3U,
    /* Payload or declared image length violates the message contract. */
    MICROFLOW_UPLOAD_ERROR_LENGTH = 4U,
    /* Frame sequence does not match the next exactly expected number. */
    MICROFLOW_UPLOAD_ERROR_SEQUENCE = 5U,
    /* Frame metadata/payload CRC does not match the transmitted CRC. */
    MICROFLOW_UPLOAD_ERROR_CHECKSUM = 6U,
    /* Message is not valid in the current upload state. */
    MICROFLOW_UPLOAD_ERROR_STATE = 7U,
    /* Data chunk is non-contiguous or extends beyond the declared image. */
    MICROFLOW_UPLOAD_ERROR_OFFSET = 8U,
    /* Fully staged image CRC differs from the value declared by BEGIN. */
    MICROFLOW_UPLOAD_ERROR_IMAGE_CHECKSUM = 9U,
    /* Board/application rejected the image's structure or capabilities. */
    MICROFLOW_UPLOAD_ERROR_VALIDATION = 10U,
    /* A complete response would not fit the bounded transmit ring. */
    MICROFLOW_UPLOAD_ERROR_TX_OVERFLOW = 11U
} microflow_upload_result_t;

typedef bool (*microflow_upload_validate_t)(void *context, const uint8_t *image,
                                             size_t length);
typedef bool (*microflow_upload_activate_t)(void *context, const uint8_t *image,
                                             size_t length);

/* All parser, staging, transfer, and diagnostic state is statically bounded. */
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

/* Clears state and binds rings plus validation/activation policy callbacks. */
void microflow_upload_transport_initialize(microflow_upload_transport_t *transport,
    microflow_byte_ring_t *receive, microflow_byte_ring_t *transmit,
    microflow_upload_validate_t validate, microflow_upload_activate_t activate,
    void *callback_context);
/* Consumes all currently queued bytes and handles every complete frame. */
void microflow_upload_transport_service(microflow_upload_transport_t *transport,
                                        uint32_t now);
/* Aborts partial parser/upload state after an unsigned wrap-safe timeout. */
bool microflow_upload_transport_expire(microflow_upload_transport_t *transport,
                                       uint32_t now, uint32_t timeout);
/* Securely clears staging state so old bytes cannot be shown or activated. */
void microflow_upload_transport_erase_staging(microflow_upload_transport_t *transport);
/* Computes the protocol's reflected IEEE CRC-32 over exactly length bytes. */
uint32_t microflow_upload_crc32(const uint8_t *data, size_t length);

#endif
