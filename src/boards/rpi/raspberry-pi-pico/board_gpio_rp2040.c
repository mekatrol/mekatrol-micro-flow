/* Binds logical Pico GPIO resources to RP2040 hardware primitives. */
#include "board_gpio_rp2040.h"
#include "rp2040_sdk.h"

/* Translate a project GPIO direction into the corresponding RP2040 setup. */
static void configure(void *context, uint32_t pin,
                      microflow_board_gpio_direction_t direction) {
    (void)context;
    /* Keep SDK-specific direction handling at the board/SoC boundary. */
    if (direction == MICROFLOW_BOARD_GPIO_OUTPUT) {
        microflow_rp2040_gpio_output_initialize(pin);
    } else {
        microflow_rp2040_gpio_input_initialize(pin);
    }
}

/* Sample a physical pin without leaking RP2040 SDK types into portable code. */
static bool read_pin(void *context, uint32_t pin) {
    (void)context;
    return microflow_rp2040_gpio_read(pin);
}

/* Drive a physical pin on behalf of the validated logical-resource facade. */
static void write_pin(void *context, uint32_t pin, bool value) {
    (void)context;
    microflow_rp2040_gpio_write(pin, value);
}

/* Bind the logical GPIO facade to one immutable RP2040 driver table. */
void microflow_board_gpio_rp2040_initialize(microflow_board_gpio_t *gpio) {
    /* Static lifetime guarantees the facade's stored driver pointer stays valid. */
    static const microflow_board_gpio_driver_t driver = {
        .context = NULL, .configure = configure, .read = read_pin, .write = write_pin
    };
    microflow_board_gpio_initialize(gpio, &driver);
}
