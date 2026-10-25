/* Public compile-time and runtime version information for Micro Flow. */
#ifndef MICROFLOW_VERSION_H
#define MICROFLOW_VERSION_H

/* Major changes identify incompatible public API revisions. */
#define MICROFLOW_VERSION_MAJOR 0
/* Minor changes identify backward-compatible feature additions. */
#define MICROFLOW_VERSION_MINOR 0
/* Patch changes identify backward-compatible fixes. */
#define MICROFLOW_VERSION_PATCH 1

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a process-lifetime semantic-version string such as "0.0.1". */
const char *microflow_version_string(void);

#ifdef __cplusplus
}
#endif

#endif
