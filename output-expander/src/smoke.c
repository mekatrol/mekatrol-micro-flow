#include "smoke.h"

bool output_expander_heartbeat_level(uint32_t elapsed_ms)
{
    /* Divide elapsed time into 500 ms windows and alternate the returned
     * level. Two windows make one complete one-second heartbeat cycle. This
     * pure helper lets host tests verify the intended timing without accessing
     * CH32V003 registers. */
    return ((elapsed_ms / UINT32_C(500)) & UINT32_C(1)) == UINT32_C(0);
}
