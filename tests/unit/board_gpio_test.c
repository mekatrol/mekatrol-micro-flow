#include "board_gpio.h"

#include <stdio.h>

typedef struct {
  bool levels[30];
  microflow_board_gpio_direction_t directions[30];
  uint32_t configure_count;
  uint32_t write_count;
} fixture_t;

static void configure(void *context, uint32_t pin,
                      microflow_board_gpio_direction_t direction) {
  fixture_t *fixture = context;
  fixture->directions[pin] = direction;
  ++fixture->configure_count;
}

static bool read_pin(void *context, uint32_t pin) {
  return ((fixture_t *)context)->levels[pin];
}

static void write_pin(void *context, uint32_t pin, bool value) {
  fixture_t *fixture = context;
  fixture->levels[pin] = value;
  ++fixture->write_count;
}

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      fprintf(stderr, "check failed line %d: %s\n", __LINE__, #condition);     \
      return 1;                                                                 \
    }                                                                           \
  } while (0)

int main(void) {
  fixture_t fixture = {0};
  microflow_board_gpio_t gpio;
  const microflow_board_gpio_driver_t driver = {
      .context = &fixture,
      .configure = configure,
      .read = read_pin,
      .write = write_pin};
  microflow_board_gpio_initialize(&gpio, &driver);

  CHECK(fixture.configure_count == MICROFLOW_BOARD_GPIO_RESOURCE_COUNT);
  CHECK(fixture.write_count == MICROFLOW_BOARD_GPIO_RESOURCE_COUNT);
  for (uint32_t pin = 2U; pin <= 5U; ++pin) {
    CHECK(fixture.directions[pin] == MICROFLOW_BOARD_GPIO_INPUT);
    CHECK(!fixture.levels[pin]);
  }

  CHECK(!microflow_board_gpio_write(&gpio, 0U, true));
  CHECK(microflow_board_gpio_configure(
      &gpio, 0U, MICROFLOW_BOARD_GPIO_OUTPUT));
  CHECK(!fixture.levels[2U]);
  CHECK(microflow_board_gpio_write(&gpio, 0U, true));
  CHECK(fixture.levels[2U]);

  bool value = false;
  CHECK(microflow_board_gpio_read(&gpio, 0U, &value));
  CHECK(value);
  CHECK(!microflow_board_gpio_configure(
      &gpio, 4U, MICROFLOW_BOARD_GPIO_OUTPUT));
  CHECK(!microflow_board_gpio_read(&gpio, 4U, &value));
  CHECK(!microflow_board_gpio_write(&gpio, 4U, true));

  microflow_board_gpio_reset_safe(&gpio);
  CHECK(fixture.directions[2U] == MICROFLOW_BOARD_GPIO_INPUT);
  CHECK(!fixture.levels[2U]);
  return 0;
}
