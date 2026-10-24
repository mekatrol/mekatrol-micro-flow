#include "board_gpio.h"

/* Logical resources deliberately hide physical pins from flow programs. */
static const uint8_t resource_pins[MICROFLOW_BOARD_GPIO_RESOURCE_COUNT] = {
    2U, 3U, 4U, 5U};

bool microflow_board_gpio_resource_pin(uint32_t resource, uint32_t *pin) {
  if ((resource >= MICROFLOW_BOARD_GPIO_RESOURCE_COUNT) || (pin == NULL)) {
    return false;
  }
  *pin = resource_pins[resource];
  return true;
}

void microflow_board_gpio_initialize(
    microflow_board_gpio_t *gpio,
    const microflow_board_gpio_driver_t *driver) {
  if ((gpio == NULL) || (driver == NULL)) {
    return;
  }
  gpio->driver = *driver;
  microflow_board_gpio_reset_safe(gpio);
}

bool microflow_board_gpio_configure(
    microflow_board_gpio_t *gpio, uint32_t resource,
    microflow_board_gpio_direction_t direction) {
  uint32_t pin = 0U;
  if ((gpio == NULL) || (gpio->driver.configure == NULL) ||
      ((direction != MICROFLOW_BOARD_GPIO_INPUT) &&
       (direction != MICROFLOW_BOARD_GPIO_OUTPUT)) ||
      !microflow_board_gpio_resource_pin(resource, &pin)) {
    return false;
  }
  if (gpio->directions[resource] == (uint8_t)direction) {
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

bool microflow_board_gpio_write(
    microflow_board_gpio_t *gpio, uint32_t resource, bool value) {
  uint32_t pin = 0U;
  if ((gpio == NULL) || (gpio->driver.write == NULL) ||
      !microflow_board_gpio_resource_pin(resource, &pin) ||
      (gpio->directions[resource] != (uint8_t)MICROFLOW_BOARD_GPIO_OUTPUT)) {
    return false;
  }
  gpio->driver.write(gpio->driver.context, pin, value);
  return true;
}

void microflow_board_gpio_reset_safe(microflow_board_gpio_t *gpio) {
  if (gpio == NULL) {
    return;
  }
  for (uint32_t resource = 0U;
       resource < MICROFLOW_BOARD_GPIO_RESOURCE_COUNT; ++resource) {
    uint32_t pin = resource_pins[resource];
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
