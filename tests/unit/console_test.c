/* Host regression tests for bounded console parsing and command dispatch. */
#include "console.h"

#include <stdio.h>
#include <string.h>

typedef struct { char output[2048]; size_t used; uint32_t writes; bool rebooted; } fixture_t;
/* Capture console responses in a bounded transcript for later assertions. */
static void output(void *context, const uint8_t *data, size_t length) {
    /* Append bytes into a bounded transcript for whole-response assertions. */
    fixture_t *f = context;
    if (f->used + length > sizeof(f->output)) length = sizeof(f->output) - f->used;
    memcpy(&f->output[f->used], data, length); f->used += length;
}
/* Model exactly one readable logical resource to exercise success and failure. */
static bool gpio_read(void *context, uint32_t resource, bool *value) {
    /* Only logical resource seven exists in this deliberately narrow fixture. */
    (void)context; if (resource != 7U) return false; *value = true; return true;
}
/* Accept one intentional write shape and count its observable side effect. */
static bool gpio_write(void *context, uint32_t resource, bool value) {
    fixture_t *f = context; if ((resource != 7U) || !value) return false; ++f->writes; return true;
}
/* Record a reboot request without resetting the host test process. */
static void reboot(void *context) { ((fixture_t *)context)->rebooted = true; }
/* Feed text through the real receive ring to preserve parser boundary behavior. */
static void feed(microflow_byte_ring_t *ring, const char *text) {
    /* Model the ISR producer by enqueuing characters in transport order. */
    while (*text != '\0') { (void)microflow_byte_ring_push_from_isr(ring, (uint8_t)*text); ++text; }
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)
/* Exercise dispatch, invalid input, reboot, GPIO, and overlong-line recovery. */
int main(void) {
    /* Wire the parser to deterministic callbacks, then feed mixed commands. */
    fixture_t fixture = {0}; microflow_byte_ring_t receive; microflow_console_t console;
    microflow_byte_ring_initialize(&receive);
    const microflow_console_operations_t operations = {.context = &fixture,
        .write_response = output, .gpio_read = gpio_read, .gpio_write = gpio_write,
        .reboot = reboot};
    microflow_console_initialize(&console, &receive, &operations);
    feed(&receive, "gpio read 7\r\ngpio write 7 1\nnope\nreboot\n");
    microflow_console_service(&console);
    fixture.output[fixture.used] = '\0';
    CHECK(strstr(fixture.output, "OK 1\r\n") != NULL);
    CHECK(fixture.writes == 1U); CHECK(fixture.rebooted); CHECK(console.commands == 4U);
    CHECK(console.invalid_commands == 1U);
    /* An overlong line is discarded in full and produces exactly one error. */
    for (size_t i = 0U; i < MICROFLOW_CONSOLE_LINE_CAPACITY + 5U; ++i)
        (void)microflow_byte_ring_push_from_isr(&receive, 'x');
    (void)microflow_byte_ring_push_from_isr(&receive, '\n');
    microflow_console_service(&console); CHECK(console.overlong_commands == 1U);
    CHECK(strstr(fixture.output, "ERR line-too-long\r\n") != NULL);
    return 0;
}
