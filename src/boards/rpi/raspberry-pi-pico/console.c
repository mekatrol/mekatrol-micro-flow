/* Allocation-free ASCII command parser for board diagnostics and control. */
#include "console.h"

/* Measure response text so the byte-oriented writer receives an explicit size. */
static size_t text_length(const char *text) {
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    return length;
}

/* Emit a command response when the application supplied an output capability. */
static void response(microflow_console_t *c, const char *text) {
    if (c->operations.write_response != NULL)
        c->operations.write_response(c->operations.context,
            (const uint8_t *)text, text_length(text));
}

/* Decide whether an input line exactly names a fixed, argument-free command. */
static bool equal(const char *a, const char *b) {
    while ((*a != '\0') && (*a == *b)) { ++a; ++b; }
    return *a == *b;
}

/* Decode a resource ID without accepting signs, whitespace, or uint32 overflow. */
static bool parse_u32(const char *text, uint32_t *value, const char **end) {
    uint32_t result = 0U;
    const char *p = text;
    if ((*p < '0') || (*p > '9')) return false;
    do {
        const uint32_t digit = (uint32_t)(*p - '0');
        if (result > (UINT32_MAX - digit) / 10U) return false;
        result = result * 10U + digit; ++p;
    } while ((*p >= '0') && (*p <= '9'));
    *value = result; *end = p; return true;
}

/* Recognize a parameterized command and return its resource plus unparsed tail. */
static bool argument(const char *line, const char *prefix, uint32_t *value,
                     const char **end) {
    while ((*prefix != '\0') && (*line == *prefix)) { ++line; ++prefix; }
    return (*prefix == '\0') && parse_u32(line, value, end);
}

/* Dispatch one complete line to the injected operation table and format a reply. */
static void command(microflow_console_t *c) {
    /* Every complete non-empty line counts, whether accepted or rejected. */
    const char *line = c->line;
    ++c->commands;
    if (equal(line, "help"))
        response(c, "OK help info status tasks queues flow show|validate|start|stop|erase-staging gpio list|read <id>|write <id> <0|1> reboot\r\n");
    else if (equal(line, "info") && c->operations.info != NULL) c->operations.info(c->operations.context);
    else if (equal(line, "status") && c->operations.status != NULL) c->operations.status(c->operations.context);
    else if (equal(line, "tasks") && c->operations.tasks != NULL) c->operations.tasks(c->operations.context);
    else if (equal(line, "queues") && c->operations.queues != NULL) c->operations.queues(c->operations.context);
    else if (equal(line, "flow show") && c->operations.flow_show != NULL) c->operations.flow_show(c->operations.context);
    else if (equal(line, "flow validate") && c->operations.flow_validate != NULL)
        response(c, c->operations.flow_validate(c->operations.context) ? "OK\r\n" : "ERR invalid-flow\r\n");
    else if (equal(line, "flow start") && c->operations.flow_start != NULL)
        response(c, c->operations.flow_start(c->operations.context) ? "OK\r\n" : "ERR unavailable\r\n");
    else if (equal(line, "flow stop") && c->operations.flow_stop != NULL)
        response(c, c->operations.flow_stop(c->operations.context) ? "OK\r\n" : "ERR unavailable\r\n");
    else if (equal(line, "flow erase-staging") && c->operations.flow_erase_staging != NULL) {
        c->operations.flow_erase_staging(c->operations.context); response(c, "OK\r\n");
    } else if (equal(line, "gpio list") && c->operations.gpio_list != NULL) c->operations.gpio_list(c->operations.context);
    else if (equal(line, "reboot") && c->operations.reboot != NULL) {
        response(c, "OK rebooting\r\n"); c->operations.reboot(c->operations.context);
    } else {
        uint32_t resource = 0U; const char *end = NULL; bool handled = false;
        if (argument(line, "gpio read ", &resource, &end) && (*end == '\0') &&
            (c->operations.gpio_read != NULL)) {
            bool value = false; handled = true;
            if (c->operations.gpio_read(c->operations.context, resource, &value))
                response(c, value ? "OK 1\r\n" : "OK 0\r\n");
            else response(c, "ERR unsupported-resource\r\n");
        } else if (argument(line, "gpio write ", &resource, &end) && (*end == ' ') &&
                   ((end[1] == '0') || (end[1] == '1')) && (end[2] == '\0') &&
                   (c->operations.gpio_write != NULL)) {
            handled = true;
            response(c, c->operations.gpio_write(c->operations.context, resource,
                end[1] == '1') ? "OK\r\n" : "ERR unsupported-resource\r\n");
        }
        if (!handled) { ++c->invalid_commands; response(c, "ERR unknown-command\r\n"); }
    }
}

/* Bind input and operations while resetting all parser and diagnostic state. */
void microflow_console_initialize(microflow_console_t *c,
    microflow_byte_ring_t *receive, const microflow_console_operations_t *operations) {
    if ((c == NULL) || (operations == NULL)) return;
    /* Copy callbacks so later caller changes cannot mutate console behavior. */
    c->receive = receive;
    c->operations.context = operations->context;
    c->operations.write_response = operations->write_response;
    c->operations.write_log = operations->write_log;
    c->operations.info = operations->info;
    c->operations.status = operations->status;
    c->operations.tasks = operations->tasks;
    c->operations.queues = operations->queues;
    c->operations.flow_show = operations->flow_show;
    c->operations.flow_validate = operations->flow_validate;
    c->operations.flow_start = operations->flow_start;
    c->operations.flow_stop = operations->flow_stop;
    c->operations.flow_erase_staging = operations->flow_erase_staging;
    c->operations.gpio_list = operations->gpio_list;
    c->operations.gpio_read = operations->gpio_read;
    c->operations.gpio_write = operations->gpio_write;
    c->operations.reboot = operations->reboot;
    c->line_length = 0U;
    c->discarding = false; c->commands = 0U; c->invalid_commands = 0U;
    c->overlong_commands = 0U;
}

/* Consume available bytes, perform line editing, and dispatch completed lines. */
void microflow_console_service(microflow_console_t *c) {
    if ((c == NULL) || (c->receive == NULL)) return;
    uint8_t byte;
    while (microflow_byte_ring_pop(c->receive, &byte)) {
        /* CR and LF each terminate a line; empty terminators are ignored. */
        if ((byte == '\r') || (byte == '\n')) {
            if (c->discarding) {
                c->discarding = false; c->line_length = 0U;
                ++c->overlong_commands; response(c, "ERR line-too-long\r\n");
            } else if (c->line_length != 0U) {
                c->line[c->line_length] = '\0'; command(c); c->line_length = 0U;
            }
        } else if ((byte == 8U) || (byte == 127U)) {
            /* Support both common terminal backspace encodings safely. */
            if (!c->discarding && (c->line_length != 0U)) --c->line_length;
        } else if (!c->discarding) {
            /* Always reserve the final array byte for a terminating NUL. */
            if (c->line_length + 1U >= MICROFLOW_CONSOLE_LINE_CAPACITY) c->discarding = true;
            else c->line[c->line_length++] = (char)byte;
        }
    }
}
