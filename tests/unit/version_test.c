#include <microflow/version.h>

#include <string.h>

int main(void)
{
    _Static_assert(MICROFLOW_VERSION_MAJOR == 0, "unexpected major version");
    _Static_assert(MICROFLOW_VERSION_MINOR == 1, "unexpected minor version");
    _Static_assert(MICROFLOW_VERSION_PATCH == 0, "unexpected patch version");

    return strcmp(microflow_version_string(), "0.1.0") == 0 ? 0 : 1;
}
