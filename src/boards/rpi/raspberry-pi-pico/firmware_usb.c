/* USB CDC transport and console service for the Pico firmware. */
#include "firmware_internal.h"

/* Queue a bounded byte span for best-effort transmission over USB CDC. */
void microflow_firmware_usb_write(microflow_firmware_t *firmware,
                                  const uint8_t *data, size_t length) {
    (void)microflow_rp2040_usb_cdc_write(&firmware->usb, data, length);
}

/* Write a NUL-terminated local string without pulling in printf machinery. */
void microflow_firmware_usb_write_text(microflow_firmware_t *firmware,
                                       const char           *text) {
    /* CDC accepts byte spans, so determine the exact wire length first. */
    size_t length = 0U;
    while (text[length] != '\0') {
        ++length;
    }
    microflow_firmware_usb_write(firmware, (const uint8_t *)text, length);
}

/* Render one unsigned counter locally so diagnostics remain bounded and avoid printf code. */
void microflow_firmware_usb_write_u32(microflow_firmware_t *firmware,
                                      uint32_t              value) {
    /* Reserve ten digits because that exactly bounds the decimal form of every uint32 value. */
    uint8_t digits[10];
    size_t  used = 0U;
    /* Collect least-significant digits first; do-while renders zero correctly. */
    do {
        digits[used++] = (uint8_t)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    /* Reverse the temporary digits to produce conventional decimal order. */
    while (used != 0U) {
        microflow_firmware_usb_write(firmware, &digits[--used], 1U);
    }
}

/* Poll and parse in main context so USB callbacks never execute command side effects in an ISR. */
void microflow_firmware_usb_service(void *context, uint32_t events) {
    microflow_firmware_t *firmware = context;
    uint8_t               value;
    (void)events;
    /*
     * Copy USB bytes into the console ring because the parser consumes that
     * ring and records overflow through the supervisor.
     */
    microflow_rp2040_usb_cdc_task(&firmware->usb);
    while (microflow_rp2040_usb_cdc_read(&firmware->usb, &value)) {
        (void)microflow_supervisor_byte_ring_push_from_isr(
            &firmware->supervisor, &firmware->console_receive, value);
    }
    microflow_console_service(&firmware->console);
}
