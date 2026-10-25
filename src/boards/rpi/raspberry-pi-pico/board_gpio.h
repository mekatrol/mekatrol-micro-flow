/* Logical-GPIO facade for flows running on the Raspberry Pi Pico board. */
#ifndef MICROFLOW_RASPBERRY_PI_PICO_BOARD_GPIO_H
#define MICROFLOW_RASPBERRY_PI_PICO_BOARD_GPIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Four resources provide a small, fixed memory footprint and stable IDs. */
#define MICROFLOW_BOARD_GPIO_RESOURCE_COUNT 4U

typedef enum {
  /* High-impedance input is the reset-safe and observation-only mode. */
  MICROFLOW_BOARD_GPIO_INPUT = 0,
  /* Push-pull output permits a flow to drive the mapped physical pin. */
  MICROFLOW_BOARD_GPIO_OUTPUT = 1
} microflow_board_gpio_direction_t;

/* Driver operations isolate logical resource policy from RP2040 access. */
typedef struct {
  /* Opaque state passed unchanged to every driver operation. */
  void *context;
  /* Applies the requested electrical direction to one physical pin. */
  void (*configure)(void *context, uint32_t pin,
                    microflow_board_gpio_direction_t direction);
  /* Samples the current logical level of one physical pin. */
  bool (*read)(void *context, uint32_t pin);
  /* Changes the output latch; callers ensure the resource is an output. */
  void (*write)(void *context, uint32_t pin, bool value);
} microflow_board_gpio_driver_t;

/* Runtime state owns a copied driver table and direction for every resource. */
typedef struct {
  microflow_board_gpio_driver_t driver;
  uint8_t directions[MICROFLOW_BOARD_GPIO_RESOURCE_COUNT];
} microflow_board_gpio_t;

/* Copies the driver and puts every mapped pin into its safe input state. */
void microflow_board_gpio_initialize(
    microflow_board_gpio_t *gpio,
    const microflow_board_gpio_driver_t *driver);
/* Validates and applies a direction; repeated requests are idempotent. */
bool microflow_board_gpio_configure(
    microflow_board_gpio_t *gpio, uint32_t resource,
    microflow_board_gpio_direction_t direction);
/* Reads a valid logical resource through the injected physical driver. */
bool microflow_board_gpio_read(
    const microflow_board_gpio_t *gpio, uint32_t resource, bool *value);
/* Writes only resources previously configured as outputs. */
bool microflow_board_gpio_write(
    microflow_board_gpio_t *gpio, uint32_t resource, bool value);
/* Drives latches low, selects input mode, and clears tracked directions. */
void microflow_board_gpio_reset_safe(microflow_board_gpio_t *gpio);
/* Resolves a stable logical ID to its board-owned physical GPIO number. */
bool microflow_board_gpio_resource_pin(uint32_t resource, uint32_t *pin);

#endif
