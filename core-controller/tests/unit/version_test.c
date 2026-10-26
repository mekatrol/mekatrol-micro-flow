/* Check compile-time and runtime version forms so tools never observe conflicting identities. */
#include <microflow/version.h>

#include <string.h>

/* Compare compile-time components and runtime text so releases expose one consistent version. */
int main(void) {
    /* Numeric components make accidental semantic-version changes explicit. */
    _Static_assert(MICROFLOW_VERSION_MAJOR == 0, "unexpected major version");
    _Static_assert(MICROFLOW_VERSION_MINOR == 0, "unexpected minor version");
    _Static_assert(MICROFLOW_VERSION_PATCH == 1, "unexpected patch version");

    /* The runtime representation shown to tools must match those components. */
    return strcmp(microflow_version_string(), "0.0.1") == 0 ? 0 : 1;
}
