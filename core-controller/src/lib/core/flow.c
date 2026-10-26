/* Allocation-free validation of the portable flow-image envelope. */
#include <microflow/flow.h>

/* Decode a flow-image uint32 independently of CPU alignment and byte order. */
static uint32_t image_u32(const uint8_t *p) {
    /* Flow image integers use an architecture-independent little-endian form. */
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

/* Calculate the reflected IEEE CRC-32 used by flow image payloads. */
static uint32_t payload_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
        }
    }
    return ~crc;
}

/* Validate structure before reading the bounded, checksummed payload. */
bool microflow_flow_validate(void *context, const uint8_t *image, size_t length) {
    (void)context;
    if ((image == NULL) || (length < MICROFLOW_FLOW_IMAGE_HEADER_SIZE) ||
        (image[0] != 'M') || (image[1] != 'F') || (image[2] != 'F') ||
        (image[3] != 'L') || (image[4] != MICROFLOW_FLOW_IMAGE_VERSION) ||
        (image[5] != 0U) ||
        (image[6] != MICROFLOW_FLOW_IMAGE_HEADER_SIZE) || (image[7] != 0U) ||
        (image_u32(&image[8]) != length)) {
        return false;
    }
    return image_u32(&image[12]) ==
           payload_crc32(&image[MICROFLOW_FLOW_IMAGE_HEADER_SIZE],
                         length - MICROFLOW_FLOW_IMAGE_HEADER_SIZE);
}
