#ifndef MICROFLOW_WAVESHARE_RP2040_ZERO_BOARD_GPIO_H
#define MICROFLOW_WAVESHARE_RP2040_ZERO_BOARD_GPIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MICROFLOW_BOARD_GPIO_RESOURCE_COUNT 4U

typedef enum {
  MICROFLOW_BOARD_GPIO_INPUT = 0,
  MICROFLOW_BOARD_GPIO_OUTPUT = 1
} microflow_board_gpio_direction_t;

typedef struct {
  void *context;
  void (*configure)(void *context, uint32_t pin,
                    microflow_board_gpio_direction_t direction);
  bool (*read)(void *context, uint32_t pin);
  void (*write)(void *context, uint32_t pin, bool value);
} microflow_board_gpio_driver_t;

typedef struct {
  microflow_board_gpio_driver_t driver;
  uint8_t directions[MICROFLOW_BOARD_GPIO_RESOURCE_COUNT];
} microflow_board_gpio_t;

void microflow_board_gpio_initialize(
    microflow_board_gpio_t *gpio,
    const microflow_board_gpio_driver_t *driver);
bool microflow_board_gpio_configure(
    microflow_board_gpio_t *gpio, uint32_t resource,
    microflow_board_gpio_direction_t direction);
bool microflow_board_gpio_read(
    const microflow_board_gpio_t *gpio, uint32_t resource, bool *value);
bool microflow_board_gpio_write(
    microflow_board_gpio_t *gpio, uint32_t resource, bool value);
void microflow_board_gpio_reset_safe(microflow_board_gpio_t *gpio);
bool microflow_board_gpio_resource_pin(uint32_t resource, uint32_t *pin);

#endif
