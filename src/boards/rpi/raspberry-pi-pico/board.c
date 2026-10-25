/* Board adaptation for the portable runtime's time, delay, and status APIs. */
#include <microflow/internal/board.h>

#include "rp2040_sdk.h"

#include <stdbool.h>

#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
/* Waveshare RP2040-Zero routes its single WS2812 status pixel to GPIO 16. */
#define STATUS_LED_PIN 16U
#else
/* The original Raspberry Pi Pico exposes its active-high LED on GPIO 25. */
#define RASPBERRY_PI_PICO_STATUS_LED_PIN 25U
/* Give both supported boards one implementation-facing LED pin name. */
#define STATUS_LED_PIN                   RASPBERRY_PI_PICO_STATUS_LED_PIN
#endif

/* Initialize SoC support and configure the board-specific status indicator. */
void microflow_board_initialize(void) {
    /* Establish clocks and low-level peripherals before touching board I/O. */
    microflow_rp2040_initialize();
#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
    /* A WS2812 requires serialized colour data rather than a GPIO level. */
    microflow_rp2040_ws2812_initialize(STATUS_LED_PIN);
#else
    /* The Pico LED is a conventional digital output. */
    microflow_rp2040_gpio_output_initialize(STATUS_LED_PIN);
#endif
}

/* Present RP2040 microseconds as the portable runtime's monotonic tick source. */
uint32_t microflow_board_monotonic_ticks(void) {
    /* Microseconds give portable scheduling a useful, deterministic unit. */
    return microflow_rp2040_monotonic_microseconds();
}

/* Map portable RGB status requests onto the indicator available on this board. */
void microflow_board_status_rgb(uint8_t red, uint8_t green, uint8_t blue) {
#if defined(MICROFLOW_WAVESHARE_RP2040_ZERO)
    /* Preserve the requested RGB colour on boards with a colour LED. */
    microflow_rp2040_ws2812_write(STATUS_LED_PIN, red, green, blue);
#else
    /* A monochrome LED represents any non-black colour as simply "on". */
    const bool enabled = (red != 0U) || (green != 0U) || (blue != 0U);
    microflow_rp2040_gpio_write(STATUS_LED_PIN, enabled);
#endif
}

/* Provide the runtime's explicit blocking delay through the RP2040 adapter. */
void microflow_board_delay_ms(uint32_t milliseconds) {
    /* Keep the SDK delay primitive behind the board boundary. */
    microflow_rp2040_delay_ms(milliseconds);
}
