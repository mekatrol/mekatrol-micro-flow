/* Expose a line console over a bounded ring so diagnostics cannot require dynamic allocation. */
#ifndef MICROFLOW_CONSOLE_H
#define MICROFLOW_CONSOLE_H

#include <microflow/supervisor.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Includes 79 command bytes plus the terminating NUL; bounds parser memory. */
#define MICROFLOW_CONSOLE_LINE_CAPACITY 80U

/* Injected operations keep parsing independent of USB and application state. */
typedef struct {
    /* Opaque application state supplied to every operation. */
    void *context;
    /* Command replies and asynchronous logs remain separate output classes. */
    void (*write_response)(void *context, const uint8_t *data, size_t length);
    void (*write_log)(void *context, const uint8_t *data, size_t length);
    void (*info)(void *context);
    void (*status)(void *context);
    void (*tasks)(void *context);
    void (*queues)(void *context);
    void (*flow_show)(void *context);
    bool (*flow_validate)(void *context);
    bool (*flow_start)(void *context);
    bool (*flow_stop)(void *context);
    void (*flow_erase_staging)(void *context);
    void (*gpio_list)(void *context);
    bool (*gpio_read)(void *context, uint32_t resource, bool *value);
    bool (*gpio_write)(void *context, uint32_t resource, bool value);
    void (*reboot)(void *context);
} microflow_console_operations_t;

/* Parser buffer, injected operations, and observable command diagnostics. */
typedef struct {
    microflow_byte_ring_t         *receive;
    microflow_console_operations_t operations;
    char                           line[MICROFLOW_CONSOLE_LINE_CAPACITY];
    uint8_t                        line_length;
    /* Prevents a truncated overlong line from becoming a valid command. */
    bool     discarding;
    uint32_t commands, invalid_commands, overlong_commands;
} microflow_console_t;

/* Clears parser/metrics state and copies the supplied operation table. */
void microflow_console_initialize(microflow_console_t   *console,
                                  microflow_byte_ring_t *receive, const microflow_console_operations_t *operations);
/* Drains bytes and dispatches every complete, non-empty command line. */
void microflow_console_service(microflow_console_t *console);

#endif
