/* Provides the library version as a stable string without allocation. */
#include <microflow/version.h>

/* Return the canonical human-readable version used by diagnostics and tools. */
const char *microflow_version_string(void) {
    /* The literal has static lifetime and is kept in sync with version.h. */
    return "0.1.0";
}
