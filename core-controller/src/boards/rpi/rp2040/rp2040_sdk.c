/* Implement project-owned RP2040 primitives with the Pico SDK so SDK types stay at this boundary. */
#include "rp2040_sdk.h"

#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

void microflow_rp2040_initialize(void) {
}

/* Claim a physical GPIO and enable its output driver for board-controlled use. */
void microflow_rp2040_gpio_output_initialize(uint32_t pin) {
    /* GPIO ownership and direction are established before application use. */
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
}

/* Set the output latch of one physical GPIO through the project boundary. */
void microflow_rp2040_gpio_write(uint32_t pin, bool high) {
    gpio_put(pin, high);
}

/* Provide an explicit millisecond blocking delay for startup/fatal paths only. */
void microflow_rp2040_delay_ms(uint32_t milliseconds) {
    sleep_ms(milliseconds);
}

/* Force a controlled chip reset through the watchdog and never return. */
void microflow_rp2040_reboot(void) {
    watchdog_reboot(0U, 0U, 0U);
    for (;;) {
        tight_loop_contents();
    }
}

/* Return the configured RP2040 system clock in hertz. */
uint32_t microflow_rp2040_system_clock_hz(void) {
    return clock_get_hz(clk_sys);
}
