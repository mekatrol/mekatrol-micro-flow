/* Streaming, allocation-free implementation of the flow upload protocol. */
#include <microflow/upload_transport.h>

/* Copy a known-valid bounded byte span without depending on a hosted C library. */
static void bytes_copy(uint8_t *destination, const uint8_t *source, size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        destination[i] = source[i];
    }
}

/* Erase a bounded byte span for parser initialization and staging disposal. */
static void bytes_zero(uint8_t *destination, size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        destination[i] = 0U;
    }
}

/* Decode a two-byte wire integer without alignment or host-endian assumptions. */
static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

/* Decode a four-byte wire integer without alignment or host-endian assumptions. */
static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

/* Encode a two-byte integer in the protocol's fixed little-endian order. */
static void write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8U);
}

/* Encode a four-byte integer in the protocol's fixed little-endian order. */
static void write_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8U);
    p[2] = (uint8_t)(v >> 16U);
    p[3] = (uint8_t)(v >> 24U);
}

/* Detect frame or image corruption with the protocol's reflected IEEE CRC-32. */
uint32_t microflow_upload_crc32(const uint8_t *data, size_t length) {
    /* All-ones seed/final inversion implement standard reflected CRC-32. */
    uint32_t crc = 0xFFFFFFFFU;
    if ((data == NULL) && (length != 0U)) {
        return 0U;
    }
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            /* 0xEDB88320 is the reflected IEEE polynomial; mask avoids a branch. */
            crc = (crc >> 1U) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
        }
    }
    return ~crc;
}

/* Append one response byte through the transport's bounded transmit ring. */
static bool tx_byte(microflow_upload_transport_t *t, uint8_t value) {
    return microflow_byte_ring_push_from_isr(t->transmit, value);
}

/* Serialize one complete checksummed reply only when the entire frame will fit. */
static bool reply(microflow_upload_transport_t *t, uint8_t type, uint32_t sequence,
                  const uint8_t *payload, uint16_t length) {
    /* Construct the complete header in local bounded storage before enqueueing. */
    uint8_t header[MICROFLOW_UPLOAD_FRAME_OVERHEAD];
    bytes_zero(header, sizeof(header));
    header[0] = 'M';
    header[1] = 'F';
    header[2] = 'L';
    header[3] = 'O';
    header[4] = MICROFLOW_UPLOAD_PROTOCOL_VERSION;
    header[5] = type;
    write_u16(&header[6], length);
    write_u32(&header[8], sequence);
    uint8_t check[8U + MICROFLOW_UPLOAD_MAX_FRAME_PAYLOAD];
    bytes_copy(check, &header[4], 8U);
    bytes_copy(&check[8], payload, length);
    write_u32(&header[12], microflow_upload_crc32(check, 8U + length));
    /* Check whole-frame capacity first so the receiver never sees a partial reply. */
    if (microflow_byte_ring_count(t->transmit) + sizeof(header) + length >=
        MICROFLOW_SUPERVISOR_BYTE_CAPACITY) {
        return false;
    }
    for (size_t i = 0U; i < sizeof(header); ++i) {
        (void)tx_byte(t, header[i]);
    }
    for (uint16_t i = 0U; i < length; ++i) {
        (void)tx_byte(t, payload[i]);
    }
    return true;
}

/* Return a compact one-byte result for a successfully parsed request type. */
static void result_reply(microflow_upload_transport_t *t, uint8_t type,
                         uint32_t sequence, microflow_upload_result_t result) {
    const uint8_t payload[] = {(uint8_t)result};
    if (!reply(t, type, sequence, payload, sizeof(payload))) {
        ++t->frames_rejected;
    }
}

/* Count a rejected frame and tell the sender the precise protocol reason. */
static void reject(microflow_upload_transport_t *t, uint32_t sequence,
                   microflow_upload_result_t result) {
    ++t->frames_rejected;
    result_reply(t, MICROFLOW_UPLOAD_ERROR, sequence, result);
}

/* Validate and apply one fully buffered frame without exposing partial state. */
static void handle_frame(microflow_upload_transport_t *t) {
    /* Header fields are trusted only after service() validates their bounds. */
    const uint8_t  type     = t->header[5];
    const uint16_t length   = read_u16(&t->header[6]);
    const uint32_t sequence = read_u32(&t->header[8]);
    uint8_t        check[8U + MICROFLOW_UPLOAD_MAX_FRAME_PAYLOAD];
    bytes_copy(check, &t->header[4], 8U);
    bytes_copy(&check[8], t->frame_payload, length);
    /* Authenticate metadata and payload before changing transfer state. */
    if (read_u32(&t->header[12]) != microflow_upload_crc32(check, 8U + length)) {
        reject(t, sequence, MICROFLOW_UPLOAD_ERROR_CHECKSUM);
        return;
    }
    /* Strict sequencing rejects duplicates, loss, and reordered frames. */
    if (sequence != t->expected_sequence) {
        reject(t, sequence, MICROFLOW_UPLOAD_ERROR_SEQUENCE);
        return;
    }
    ++t->expected_sequence;
    ++t->frames_received;
    if (type == MICROFLOW_UPLOAD_PROBE) {
        /* Capacity is encoded as two little-endian bytes in the compact reply. */
        const uint8_t info[] = {MICROFLOW_UPLOAD_PROTOCOL_VERSION,
                                (uint8_t)MICROFLOW_UPLOAD_MAX_IMAGE_SIZE,
                                (uint8_t)(MICROFLOW_UPLOAD_MAX_IMAGE_SIZE >> 8U)};
        if (length != 0U) {
            reject(t, sequence, MICROFLOW_UPLOAD_ERROR_LENGTH);
        } else if (!reply(t, MICROFLOW_UPLOAD_DEVICE_INFO, sequence, info, sizeof(info))) {
            reject(t, sequence, MICROFLOW_UPLOAD_ERROR_TX_OVERFLOW);
        }
    } else if (type == MICROFLOW_UPLOAD_BEGIN) {
        /* BEGIN replaces transfer metadata but does not alter the active image. */
        if (length != 8U) {
            reject(t, sequence, MICROFLOW_UPLOAD_ERROR_LENGTH);
            return;
        }
        const uint32_t image_length = read_u32(t->frame_payload);
        if ((image_length == 0U) || (image_length > MICROFLOW_UPLOAD_MAX_IMAGE_SIZE)) {
            reject(t, sequence, MICROFLOW_UPLOAD_ERROR_LENGTH);
            return;
        }
        t->image_length    = image_length;
        t->image_checksum  = read_u32(&t->frame_payload[4]);
        t->received_length = 0U;
        t->upload_active   = true;
        result_reply(t, MICROFLOW_UPLOAD_BEGIN, sequence, MICROFLOW_UPLOAD_OK);
    } else if (type == MICROFLOW_UPLOAD_DATA) {
        /* Contiguous-only writes simplify bounds checking and retransmission. */
        if (!t->upload_active) {
            reject(t, sequence, MICROFLOW_UPLOAD_ERROR_STATE);
            return;
        }
        if (length < 5U) {
            reject(t, sequence, MICROFLOW_UPLOAD_ERROR_LENGTH);
            return;
        }
        const uint32_t offset = read_u32(t->frame_payload);
        const uint32_t chunk  = (uint32_t)length - 4U;
        if ((offset != t->received_length) || (chunk > t->image_length - t->received_length)) {
            reject(t, sequence, MICROFLOW_UPLOAD_ERROR_OFFSET);
            return;
        }
        bytes_copy(&t->staging[offset], &t->frame_payload[4], chunk);
        t->received_length += chunk;
        result_reply(t, MICROFLOW_UPLOAD_DATA, sequence, MICROFLOW_UPLOAD_OK);
    } else if (type == MICROFLOW_UPLOAD_COMPLETE) {
        /* Completion is legal only after the declared number of bytes arrives. */
        if ((length != 0U) || !t->upload_active ||
            (t->received_length != t->image_length)) {
            reject(t, sequence, length != 0U ? MICROFLOW_UPLOAD_ERROR_LENGTH : MICROFLOW_UPLOAD_ERROR_STATE);
            return;
        }
        if (microflow_upload_crc32(t->staging, t->image_length) != t->image_checksum) {
            t->upload_active = false;
            reject(t, sequence, MICROFLOW_UPLOAD_ERROR_IMAGE_CHECKSUM);
            return;
        }
        /* Validate before activate so malformed flows never become executable. */
        const bool valid = t->validate != NULL &&
                           t->validate(t->callback_context, t->staging, t->image_length);
        result_reply(t, MICROFLOW_UPLOAD_VALIDATION_RESULT, sequence,
                     valid ? MICROFLOW_UPLOAD_OK : MICROFLOW_UPLOAD_ERROR_VALIDATION);
        if (!valid) {
            t->upload_active = false;
            return;
        }
        const bool activated = t->activate != NULL &&
                               t->activate(t->callback_context, t->staging, t->image_length);
        result_reply(t, MICROFLOW_UPLOAD_ACTIVATION_RESULT, sequence,
                     activated ? MICROFLOW_UPLOAD_OK : MICROFLOW_UPLOAD_ERROR_STATE);
        if (activated) {
            ++t->uploads_completed;
        }
        t->upload_active = false;
    } else if (type == MICROFLOW_UPLOAD_ABORT) {
        /* Abort retains staging bytes for diagnostics but makes them inactive. */
        if (length != 0U) {
            reject(t, sequence, MICROFLOW_UPLOAD_ERROR_LENGTH);
            return;
        }
        t->upload_active   = false;
        t->received_length = 0U;
        ++t->uploads_aborted;
        result_reply(t, MICROFLOW_UPLOAD_ABORT, sequence, MICROFLOW_UPLOAD_OK);
    } else {
        reject(t, sequence, MICROFLOW_UPLOAD_ERROR_TYPE);
    }
}

/* Return framing state to magic search while leaving upload staging intact. */
static void reset_parser(microflow_upload_transport_t *t) {
    t->header_used  = 0U;
    t->payload_used = 0U;
}

/* Construct empty transport state and bind caller-owned queues and policies. */
void microflow_upload_transport_initialize(microflow_upload_transport_t *t,
                                           microflow_byte_ring_t *receive, microflow_byte_ring_t *transmit,
                                           microflow_upload_validate_t validate, microflow_upload_activate_t activate,
                                           void *callback_context) {
    if (t == NULL) {
        return;
    }
    bytes_zero((uint8_t *)t, sizeof(*t));
    t->receive          = receive;
    t->transmit         = transmit;
    t->validate         = validate;
    t->activate         = activate;
    t->callback_context = callback_context;
}

/* Drain received bytes, resynchronize on magic, and handle each complete frame. */
void microflow_upload_transport_service(microflow_upload_transport_t *t, uint32_t now) {
    if ((t == NULL) || (t->receive == NULL) || (t->transmit == NULL)) {
        return;
    }
    uint8_t byte;
    while (microflow_byte_ring_pop(t->receive, &byte)) {
        /* Any received byte refreshes the timeout for a partial transaction. */
        t->last_receive_tick  = now;
        t->receive_tick_valid = true;
        if (t->header_used < MICROFLOW_UPLOAD_FRAME_OVERHEAD) {
            /* Scan for magic incrementally, retaining an overlapping leading M. */
            static const uint8_t magic[4] = {'M', 'F', 'L', 'O'};
            if ((t->header_used < 4U) && (byte != magic[t->header_used])) {
                t->header_used = byte == magic[0] ? 1U : 0U;
                ++t->frames_rejected;
                continue;
            }
            t->header[t->header_used++] = byte;
            if (t->header_used == MICROFLOW_UPLOAD_FRAME_OVERHEAD) {
                const uint16_t length = read_u16(&t->header[6]);
                /* Reject unsafe lengths before writing the payload buffer. */
                if (t->header[4] != MICROFLOW_UPLOAD_PROTOCOL_VERSION) {
                    reject(t, read_u32(&t->header[8]), MICROFLOW_UPLOAD_ERROR_VERSION);
                    reset_parser(t);
                } else if (length > MICROFLOW_UPLOAD_MAX_FRAME_PAYLOAD) {
                    reject(t, read_u32(&t->header[8]), MICROFLOW_UPLOAD_ERROR_LENGTH);
                    reset_parser(t);
                } else if (length == 0U) {
                    handle_frame(t);
                    reset_parser(t);
                }
            }
        } else {
            t->frame_payload[t->payload_used++] = byte;
            if (t->payload_used == read_u16(&t->header[6])) {
                handle_frame(t);
                reset_parser(t);
            }
        }
    }
}

/* Abandon a stalled partial frame or upload so finite state cannot hang forever. */
bool microflow_upload_transport_expire(microflow_upload_transport_t *t,
                                       uint32_t now, uint32_t timeout) {
    /* Unsigned subtraction intentionally remains valid across timer wrap. */
    if ((t == NULL) || !t->receive_tick_valid ||
        ((uint32_t)(now - t->last_receive_tick) < timeout) ||
        ((t->header_used == 0U) && !t->upload_active)) {
        return false;
    }
    reset_parser(t);
    if (t->upload_active) {
        t->upload_active = false;
        ++t->uploads_aborted;
    }
    t->receive_tick_valid = false;
    return true;
}

/* Remove all staged bytes and parser metadata without altering the active flow. */
void microflow_upload_transport_erase_staging(microflow_upload_transport_t *t) {
    if (t == NULL) {
        return;
    }
    bytes_zero(t->staging, sizeof(t->staging));
    t->upload_active      = false;
    t->image_length       = 0U;
    t->image_checksum     = 0U;
    t->received_length    = 0U;
    t->receive_tick_valid = false;
    reset_parser(t);
}
