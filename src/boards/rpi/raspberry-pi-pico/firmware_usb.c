
/* USB CDC transport and console service for the Pico firmware. */
#include "firmware_internal.h"

/* Queue a bounded byte span for best-effort transmission over USB CDC. */
void microflow_firmware_usb_write(microflow_firmware_t *firmware,
                                  const uint8_t        *data,
                                  size_t                length) {
    (void)microflow_rp2040_usb_cdc_write(
        &firmware->usb, data, length);
}

/* Write a NUL-terminated local string without printf. */
void microflow_firmware_usb_write_text(microflow_firmware_t *firmware,
                                       const char           *text) {
    size_t length = 0U;

    while (text[length] != '\0') {
        ++length;
    }

    microflow_firmware_usb_write(
        firmware, (const uint8_t *)text, length);
}

/* Write an unsigned 32-bit integer in decimal. */
void microflow_firmware_usb_write_u32(microflow_firmware_t *firmware,
                                      uint32_t              value) {
    uint8_t digits[10];
    size_t  used = 0U;

    do {
        digits[used++] = (uint8_t)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);

    while (used != 0U) {
        microflow_firmware_usb_write(
            firmware, &digits[--used], 1U);
    }
}

/*
 * Process USB controller events and console input.
 *
 * Called exclusively by the FreeRTOS USB task.
 */
void microflow_firmware_usb_service(void *context) {
    microflow_firmware_t *firmware = context;
    uint8_t               value;

    microflow_rp2040_usb_cdc_task(&firmware->usb);

    /*
     * Drain all received bytes, including when the console ring is full.
     * Overflow rejects the newest byte without overwriting unread input.
     */
    while (microflow_rp2040_usb_cdc_read(&firmware->usb, &value)) {
        (void)microflow_byte_ring_push_from_isr(
            &firmware->console_receive,
            value);
    }

    microflow_console_service(&firmware->console);
}