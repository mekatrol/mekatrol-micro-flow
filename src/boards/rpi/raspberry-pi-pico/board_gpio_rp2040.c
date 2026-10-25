/* Binds logical Pico GPIO resources to RP2040 hardware primitives. */
#include "board_gpio_rp2040.h"
#include "rp2040_sdk.h"

static void configure(void *context, uint32_t pin,
                      microflow_board_gpio_direction_t direction) {
    (void)context;
    if (direction == MICROFLOW_BOARD_GPIO_OUTPUT) {
        microflow_rp2040_gpio_output_initialize(pin);
    } else {
        microflow_rp2040_gpio_input_initialize(pin);
    }
}

static bool read_pin(void *context, uint32_t pin) {
    (void)context;
    return microflow_rp2040_gpio_read(pin);
}

static void write_pin(void *context, uint32_t pin, bool value) {
    (void)context;
    microflow_rp2040_gpio_write(pin, value);
}

void microflow_board_gpio_rp2040_initialize(microflow_board_gpio_t *gpio) {
    static const microflow_board_gpio_driver_t driver = {
        .context = NULL, .configure = configure, .read = read_pin, .write = write_pin};
    microflow_board_gpio_initialize(gpio, &driver);
}
