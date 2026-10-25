/* Portable flow-image format definitions and validation. */
#ifndef MICROFLOW_FLOW_H
#define MICROFLOW_FLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Wire-format revision accepted by the flow-image validator. */
#define MICROFLOW_FLOW_IMAGE_VERSION 1U
/* Bytes occupied by the flow image's magic, metadata, length, and checksum. */
#define MICROFLOW_FLOW_IMAGE_HEADER_SIZE 16U

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Validates a complete flow image without allocation or platform services.
 * Context is reserved for callback compatibility and may be NULL.
 */
bool microflow_flow_validate(void *context, const uint8_t *image, size_t length);

#ifdef __cplusplus
}
#endif

#endif
