/* Host regression tests for logical GPIO mapping, safety, and validation. */
#include "board_gpio.h"

#include <stdio.h>

typedef struct {
    /* In-memory physical state makes the test independent of GPIO hardware. */
    bool                             levels[30];
    microflow_board_gpio_direction_t directions[30];
    uint32_t                         configure_count;
    uint32_t                         write_count;
} fixture_t;

/* Record requested physical direction changes made by logical GPIO policy. */
static void configure(void *context, uint32_t pin,
                      microflow_board_gpio_direction_t direction) {
    /* Record direction and call count to expose redundant or missing writes. */
    fixture_t *fixture       = context;
    fixture->directions[pin] = direction;
    ++fixture->configure_count;
}

/* Return fixture pin state as the board driver's hardware sample. */
static bool read_pin(void *context, uint32_t pin) {
    return ((fixture_t *)context)->levels[pin];
}

/* Record output-latch changes so safe transition ordering can be asserted. */
static void write_pin(void *context, uint32_t pin, bool value) {
    fixture_t *fixture   = context;
    fixture->levels[pin] = value;
    ++fixture->write_count;
}

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            fprintf(stderr, "check failed line %d: %s\n", __LINE__, #condition); \
            return 1;                                                            \
        }                                                                        \
    } while (0)

/* Exercise mapping, safe initialization, access policy, and safe reset. */
int main(void) {
    /* One scenario covers safe reset, mapping, direction, read, and write policy. */
    fixture_t                           fixture = {0};
    microflow_board_gpio_t              gpio;
    const microflow_board_gpio_driver_t driver = {
        .context   = &fixture,
        .configure = configure,
        .read      = read_pin,
        .write     = write_pin};
    microflow_board_gpio_initialize(&gpio, &driver);

    /* Initialization must put all mapped pins in low-latched input mode. */
    CHECK(fixture.configure_count == MICROFLOW_BOARD_GPIO_RESOURCE_COUNT);
    CHECK(fixture.write_count == MICROFLOW_BOARD_GPIO_RESOURCE_COUNT);
    for (uint32_t pin = 2U; pin <= 5U; ++pin) {
        CHECK(fixture.directions[pin] == MICROFLOW_BOARD_GPIO_INPUT);
        CHECK(!fixture.levels[pin]);
    }

    /* Driving is forbidden until that logical resource becomes an output. */
    CHECK(!microflow_board_gpio_write(&gpio, 0U, true));
    CHECK(microflow_board_gpio_configure(
        &gpio, 0U, MICROFLOW_BOARD_GPIO_OUTPUT));
    CHECK(!fixture.levels[2U]);
    CHECK(microflow_board_gpio_write(&gpio, 0U, true));
    CHECK(fixture.levels[2U]);

    bool value = false;
    CHECK(microflow_board_gpio_read(&gpio, 0U, &value));
    CHECK(value);
    /* Out-of-range logical IDs fail without reaching the physical driver. */
    CHECK(!microflow_board_gpio_configure(
        &gpio, 4U, MICROFLOW_BOARD_GPIO_OUTPUT));
    CHECK(!microflow_board_gpio_read(&gpio, 4U, &value));
    CHECK(!microflow_board_gpio_write(&gpio, 4U, true));

    /* Explicit reset must undo a previously asserted output. */
    microflow_board_gpio_reset_safe(&gpio);
    CHECK(fixture.directions[2U] == MICROFLOW_BOARD_GPIO_INPUT);
    CHECK(!fixture.levels[2U]);
    return 0;
}
