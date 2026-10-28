#ifndef OUTPUT_EXPANDER_SMOKE_H
#define OUTPUT_EXPANDER_SMOKE_H

#include <stdbool.h>
#include <stdint.h>

/* Return the logical heartbeat level for an elapsed millisecond timestamp.
 * Keeping timing policy free of register access makes boundary behavior
 * directly testable on a desktop host. */
bool output_expander_heartbeat_level(uint32_t elapsed_ms);

#endif
