/* Board adaptation for the portable runtime's time, delay, and status APIs. */
#include <microflow/internal/board.h>

#include "rp2040_sdk.h"
#include "rp2_ws2812.h"

#include <stdbool.h>

#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
/* Waveshare RP2040-Zero routes its single WS2812 status pixel to GPIO 16. */
#define STATUS_LED_PIN 16U
#else
/* Select GPIO 25 because the original Pico wires its active-high LED there. */
#define RASPBERRY_PI_PICO_STATUS_LED_PIN 25U
/* Use one internal pin name so later status code is independent of the board variant. */
#define STATUS_LED_PIN                   RASPBERRY_PI_PICO_STATUS_LED_PIN
#endif

#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
static rp2_ws2812_strip_t onboard_ws2812_led;
#endif

/* Initialize the SoC before its status output so no board I/O precedes clock setup. */
void microflow_board_initialize(void) {
    /* Establish clocks and low-level peripherals before touching board I/O. */
    microflow_rp2040_initialize();
#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
    /* A WS2812 requires serialized colour data rather than a GPIO level. */
    rp2_ws2812_init(&onboard_ws2812_led, STATUS_LED_PIN);
#else
    /* Configure a digital output because the Pico LED needs no serialized colour driver. */
    microflow_rp2040_gpio_output_initialize(STATUS_LED_PIN);
#endif
}

/* Map portable RGB status requests onto the indicator available on this board. */
void microflow_board_status_rgb(uint8_t red, uint8_t green, uint8_t blue) {
#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
    /* Forward all RGB components because the WS2812 can represent the requested colour. */
    rp2_ws2812_write_rgb(&onboard_ws2812_led, red, green, blue);
#else
    /* Collapse non-black RGB to "on" because a monochrome Pico LED has no colour channels. */
    const bool enabled = (red != 0U) || (green != 0U) || (blue != 0U);
    microflow_rp2040_gpio_write(STATUS_LED_PIN, enabled);
#endif
}

/* Provide the runtime's explicit blocking delay through the RP2040 adapter. */
void microflow_board_delay_ms(uint32_t milliseconds) {
    /* Keep the SDK delay primitive behind the board boundary. */
    microflow_rp2040_delay_ms(milliseconds);
}
