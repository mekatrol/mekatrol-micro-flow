/* Validate logical GPIO access here so callers cannot address unmapped physical pins. */
#include "board_gpio.h"

/* Map logical resources here so flow programs survive changes to physical board pins. */
static const uint8_t resource_pins[MICROFLOW_BOARD_GPIO_RESOURCE_COUNT] = {
    2U, 3U, 4U, 5U
};

/* Resolve an externally visible logical resource into its fixed board pin. */
bool microflow_board_gpio_resource_pin(uint32_t resource, uint32_t *pin) {
    if ((resource >= MICROFLOW_BOARD_GPIO_RESOURCE_COUNT) || (pin == NULL)) {
        /* Reject invalid IDs and output pointers without touching hardware. */
        return false;
    }
    *pin = resource_pins[resource];
    return true;
}

/* Construct logical-GPIO state and establish safe electrical defaults. */
void microflow_board_gpio_initialize(
    microflow_board_gpio_t              *gpio,
    const microflow_board_gpio_driver_t *driver) {
    if ((gpio == NULL) || (driver == NULL)) {
        /* Initialization cannot safely construct partial driver state. */
        return;
    }
    gpio->driver = *driver;
    /* Latch low before selecting input so a later output transition cannot begin high. */
    microflow_board_gpio_reset_safe(gpio);
}

/* Change one resource direction without redundant hardware reconfiguration. */
bool microflow_board_gpio_configure(
    microflow_board_gpio_t *gpio, uint32_t resource,
    microflow_board_gpio_direction_t direction) {
    uint32_t pin = 0U;
    if ((gpio == NULL) || (gpio->driver.configure == NULL) ||
        ((direction != MICROFLOW_BOARD_GPIO_INPUT) &&
         (direction != MICROFLOW_BOARD_GPIO_OUTPUT)) ||
        !microflow_board_gpio_resource_pin(resource, &pin)) {
        /* Validate every capability before making the first hardware change. */
        return false;
    }
    if (gpio->directions[resource] == (uint8_t)direction) {
        /* Avoid redundant register writes and observable driver side effects. */
        return true;
    }
    /* Establish a low output latch before enabling output drive. */
    if ((direction == MICROFLOW_BOARD_GPIO_OUTPUT) &&
        (gpio->driver.write != NULL)) {
        gpio->driver.write(gpio->driver.context, pin, false);
    }
    gpio->driver.configure(gpio->driver.context, pin, direction);
    gpio->directions[resource] = (uint8_t)direction;
    return true;
}

/* Sample a mapped resource only when the driver exposes read capability. */
bool microflow_board_gpio_read(
    const microflow_board_gpio_t *gpio, uint32_t resource, bool *value) {
    uint32_t pin = 0U;
    if ((gpio == NULL) || (value == NULL) || (gpio->driver.read == NULL) ||
        !microflow_board_gpio_resource_pin(resource, &pin)) {
        return false;
    }
    *value = gpio->driver.read(gpio->driver.context, pin);
    return true;
}

/* Drive a mapped resource only after it has explicitly become an output. */
bool microflow_board_gpio_write(
    microflow_board_gpio_t *gpio, uint32_t resource, bool value) {
    uint32_t pin = 0U;
    if ((gpio == NULL) || (gpio->driver.write == NULL) ||
        !microflow_board_gpio_resource_pin(resource, &pin) ||
        (gpio->directions[resource] != (uint8_t)MICROFLOW_BOARD_GPIO_OUTPUT)) {
        /* Never drive an unknown, unsupported, or input-configured resource. */
        return false;
    }
    gpio->driver.write(gpio->driver.context, pin, value);
    return true;
}

/* Return every resource to a low-latched input after stop, reset, or failure. */
void microflow_board_gpio_reset_safe(microflow_board_gpio_t *gpio) {
    if (gpio == NULL) {
        return;
    }
    for (uint32_t resource = 0U;
         resource < MICROFLOW_BOARD_GPIO_RESOURCE_COUNT; ++resource) {
        uint32_t pin = resource_pins[resource];
        /* Clear the latch first so a future output transition begins low. */
        if (gpio->driver.write != NULL) {
            gpio->driver.write(gpio->driver.context, pin, false);
        }
        if (gpio->driver.configure != NULL) {
            gpio->driver.configure(gpio->driver.context, pin,
                                   MICROFLOW_BOARD_GPIO_INPUT);
        }
        gpio->directions[resource] = (uint8_t)MICROFLOW_BOARD_GPIO_INPUT;
    }
}
