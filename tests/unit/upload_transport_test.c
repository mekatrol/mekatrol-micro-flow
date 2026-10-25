/* Host regression tests for framed uploads, rejection, and resynchronization. */
#include <microflow/supervisor.h>
#include <microflow/upload_transport.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)           \
    do {                   \
        if (!(c)) abort(); \
    } while (0)

typedef struct {
    uint8_t  active[64];
    size_t   length;
    uint32_t validations, activations;
} fixture_t;

/* Model semantic flow validation and count how often it is requested. */
static bool validate(void *context, const uint8_t *image, size_t length) {
    /* A FLOW prefix stands in for full semantic validation in this fixture. */
    fixture_t *f = context;
    ++f->validations;
    return length >= 4U && memcmp(image, "FLOW", 4U) == 0;
}
/* Model atomic activation by copying accepted bytes into fixture-owned storage. */
static bool activate(void *context, const uint8_t *image, size_t length) {
    fixture_t *f = context;
    ++f->activations;
    if (length > sizeof(f->active)) {
        return false;
    }
    memcpy(f->active, image, length);
    f->length = length;
    return true;
}
/* Encode test metadata in the protocol's two-byte little-endian representation. */
static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8U);
}
/* Encode test metadata in the protocol's four-byte little-endian representation. */
static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8U);
    p[2] = (uint8_t)(v >> 16U);
    p[3] = (uint8_t)(v >> 24U);
}
/* Build a valid framed request so individual tests can mutate targeted fields. */
static size_t frame(uint8_t *out, uint8_t type, uint32_t sequence,
                    const uint8_t *payload, uint16_t length) {
    /* Serialize the exact little-endian wire representation under test. */
    memcpy(out, "MFLO", 4U);
    out[4] = 1U;
    out[5] = type;
    put_u16(&out[6], length);
    put_u32(&out[8], sequence);
    if (length != 0U) {
        memcpy(&out[16], payload, length);
    }
    uint8_t check[8U + MICROFLOW_UPLOAD_MAX_FRAME_PAYLOAD];
    memcpy(check, &out[4], 8U);
    if (length != 0U) {
        memcpy(&check[8], payload, length);
    }
    put_u32(&out[12], microflow_upload_crc32(check, 8U + length));
    return 16U + length;
}
/* Feed a complete byte span through the same bounded ring used by firmware. */
static void send(microflow_upload_transport_t *t, microflow_byte_ring_t *rx,
                 const uint8_t *bytes, size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        CHECK(microflow_byte_ring_push_from_isr(rx, bytes[i]));
    }
    microflow_upload_transport_service(t, 10U);
}
/* Drain replies when a scenario cares about state rather than response bytes. */
static void discard(microflow_byte_ring_t *ring) {
    uint8_t value;
    while (microflow_byte_ring_pop(ring, &value)) {
    }
}

/* Prove the successful begin/data/complete path and explicit abort behavior. */
static void test_valid_upload_and_abort(void) {
    /* Complete one activation, then prove abort clears transfer state. */
    microflow_byte_ring_t        rx, tx;
    microflow_upload_transport_t t;
    fixture_t                    f = {{0}, 0U, 0U, 0U};
    microflow_byte_ring_initialize(&rx);
    microflow_byte_ring_initialize(&tx);
    microflow_upload_transport_initialize(&t, &rx, &tx, validate, activate, &f);
    const uint8_t image[] = "FLOW-valid";
    uint8_t       bytes[300], payload[32];
    put_u32(payload, sizeof(image));
    put_u32(&payload[4], microflow_upload_crc32(image, sizeof(image)));
    size_t n = frame(bytes, MICROFLOW_UPLOAD_BEGIN, 0U, payload, 8U);
    send(&t, &rx, bytes, n);
    discard(&tx);
    put_u32(payload, 0U);
    memcpy(&payload[4], image, sizeof(image));
    n = frame(bytes, MICROFLOW_UPLOAD_DATA, 1U, payload, (uint16_t)(4U + sizeof(image)));
    send(&t, &rx, bytes, n);
    discard(&tx);
    n = frame(bytes, MICROFLOW_UPLOAD_COMPLETE, 2U, NULL, 0U);
    send(&t, &rx, bytes, n);
    CHECK(f.validations == 1U && f.activations == 1U && f.length == sizeof(image));
    CHECK(memcmp(f.active, image, sizeof(image)) == 0 && t.uploads_completed == 1U);
    discard(&tx);
    n = frame(bytes, MICROFLOW_UPLOAD_BEGIN, 3U, payload, 8U);
    send(&t, &rx, bytes, n);
    discard(&tx);
    n = frame(bytes, MICROFLOW_UPLOAD_ABORT, 4U, NULL, 0U);
    send(&t, &rx, bytes, n);
    CHECK(!t.upload_active && t.uploads_aborted == 1U);
}

/* Prove invalid frames and chunks cannot alter an already active flow image. */
static void test_rejections_preserve_active(void) {
    /* Invalid checksum, sequence, and offset must preserve the active image. */
    microflow_byte_ring_t        rx, tx;
    microflow_upload_transport_t t;
    fixture_t                    f = {{1U, 2U}, 2U, 0U, 0U};
    microflow_byte_ring_initialize(&rx);
    microflow_byte_ring_initialize(&tx);
    microflow_upload_transport_initialize(&t, &rx, &tx, validate, activate, &f);
    uint8_t bytes[300], payload[32];
    size_t  n = frame(bytes, MICROFLOW_UPLOAD_PROBE, 0U, NULL, 0U);
    bytes[12] ^= 1U;
    send(&t, &rx, bytes, n);
    CHECK(t.expected_sequence == 0U);
    discard(&tx);
    n = frame(bytes, MICROFLOW_UPLOAD_PROBE, 1U, NULL, 0U);
    send(&t, &rx, bytes, n);
    CHECK(t.expected_sequence == 0U);
    discard(&tx);
    put_u32(payload, 5U);
    put_u32(&payload[4], 0U);
    n = frame(bytes, MICROFLOW_UPLOAD_BEGIN, 0U, payload, 8U);
    send(&t, &rx, bytes, n);
    discard(&tx);
    put_u32(payload, 1U);
    memcpy(&payload[4], "FLOW", 4U);
    n = frame(bytes, MICROFLOW_UPLOAD_DATA, 1U, payload, 8U);
    send(&t, &rx, bytes, n);
    CHECK(t.received_length == 0U && f.length == 2U);
    discard(&tx);
    n = frame(bytes, MICROFLOW_UPLOAD_COMPLETE, 2U, NULL, 0U);
    send(&t, &rx, bytes, n);
    CHECK(f.activations == 0U && f.active[0] == 1U && f.active[1] == 2U);
}

/* Prove bounded parsing recovers after noise, oversize metadata, and timeout. */
static void test_oversized_and_resynchronization(void) {
    /* Recover from noise, unsafe lengths, and a timed-out partial frame. */
    microflow_byte_ring_t        rx, tx;
    microflow_upload_transport_t t;
    fixture_t                    f = {{0}, 0U, 0U, 0U};
    microflow_byte_ring_initialize(&rx);
    microflow_byte_ring_initialize(&tx);
    microflow_upload_transport_initialize(&t, &rx, &tx, validate, activate, &f);
    uint8_t bytes[32] = {'x', 'M', 'F', 'L', 'O', 1U, MICROFLOW_UPLOAD_PROBE};
    put_u16(&bytes[7], MICROFLOW_UPLOAD_MAX_FRAME_PAYLOAD + 1U);
    put_u32(&bytes[9], 0U);
    put_u32(&bytes[13], 0U);
    send(&t, &rx, bytes, 17U);
    discard(&tx);
    size_t n = frame(bytes, MICROFLOW_UPLOAD_PROBE, 0U, NULL, 0U);
    send(&t, &rx, bytes, n);
    CHECK(t.frames_received == 1U && t.expected_sequence == 1U);
    discard(&tx);
    n = frame(bytes, MICROFLOW_UPLOAD_PROBE, 1U, NULL, 0U);
    for (size_t i = 0U; i < 8U; ++i) {
        CHECK(microflow_byte_ring_push_from_isr(&rx, bytes[i]));
    }
    microflow_upload_transport_service(&t, 20U);
    CHECK(!microflow_upload_transport_expire(&t, 25U, 10U));
    CHECK(microflow_upload_transport_expire(&t, 30U, 10U));
    send(&t, &rx, bytes, n);
    CHECK(t.frames_received == 2U);
}

/* Run independent upload protocol scenarios as one host regression binary. */
int main(void) {
    test_valid_upload_and_abort();
    test_rejections_preserve_active();
    test_oversized_and_resynchronization();
    return 0;
}
