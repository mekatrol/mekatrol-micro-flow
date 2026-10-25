/* Host regression tests for portable flow-image validation. */
#include <microflow/flow.h>
#include <microflow/flow_store.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)           \
    do {                   \
        if (!(c)) abort(); \
    } while (0)

static bool stopped;

static void on_stopped(void *context) {
    bool *called = context;
    *called      = true;
}

static void put_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

static uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
        }
    }
    return ~crc;
}

static void make_image(uint8_t *image, size_t length) {
    memcpy(image, "MFFL", 4U);
    image[4] = MICROFLOW_FLOW_IMAGE_VERSION;
    image[5] = 0U;
    image[6] = MICROFLOW_FLOW_IMAGE_HEADER_SIZE;
    image[7] = 0U;
    put_u32(&image[8], (uint32_t)length);
    put_u32(&image[12], crc32(&image[MICROFLOW_FLOW_IMAGE_HEADER_SIZE],
                              length - MICROFLOW_FLOW_IMAGE_HEADER_SIZE));
}

int main(void) {
    uint8_t image[MICROFLOW_FLOW_IMAGE_HEADER_SIZE + 4U] = {0};
    memcpy(&image[MICROFLOW_FLOW_IMAGE_HEADER_SIZE], "test", 4U);
    make_image(image, sizeof(image));
    CHECK(microflow_flow_validate(NULL, image, sizeof(image)));

    uint8_t                storage[sizeof(image)];
    microflow_flow_store_t store;
    microflow_flow_store_initialize(&store, storage, sizeof(storage), NULL,
                                    NULL, on_stopped, &stopped);
    CHECK(microflow_flow_store_activate(&store, image, sizeof(image)));
    CHECK(microflow_flow_store_validate(&store));
    CHECK(microflow_flow_store_start(&store) && store.running);
    microflow_flow_store_stop(&store);
    CHECK(!store.running && stopped);

    image[0] = 'X';
    CHECK(!microflow_flow_validate(NULL, image, sizeof(image)));
    image[0] = 'M';
    image[4] = 2U;
    CHECK(!microflow_flow_validate(NULL, image, sizeof(image)));
    image[4] = MICROFLOW_FLOW_IMAGE_VERSION;
    image[8]--;
    CHECK(!microflow_flow_validate(NULL, image, sizeof(image)));
    image[8]++;
    image[sizeof(image) - 1U] ^= 1U;
    CHECK(!microflow_flow_validate(NULL, image, sizeof(image)));
    CHECK(!microflow_flow_validate(NULL, NULL, sizeof(image)));
    CHECK(!microflow_flow_validate(NULL, image,
                                   MICROFLOW_FLOW_IMAGE_HEADER_SIZE - 1U));
    return 0;
}
