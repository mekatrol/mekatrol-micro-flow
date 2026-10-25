/* Compile-time and runtime checks that public version forms remain aligned. */
#include <microflow/version.h>

#include <string.h>

/* Verify compile-time version components and the runtime string stay aligned. */
int main(void)
{
    /* Numeric components make accidental semantic-version changes explicit. */
    _Static_assert(MICROFLOW_VERSION_MAJOR == 0, "unexpected major version");
    _Static_assert(MICROFLOW_VERSION_MINOR == 1, "unexpected minor version");
    _Static_assert(MICROFLOW_VERSION_PATCH == 0, "unexpected patch version");

    /* The runtime representation shown to tools must match those components. */
    return strcmp(microflow_version_string(), "0.1.0") == 0 ? 0 : 1;
}
