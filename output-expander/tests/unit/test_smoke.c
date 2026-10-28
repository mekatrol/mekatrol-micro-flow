#include "smoke.h"

#include <assert.h>

int main(void)
{
    /* Check both sides of each transition so an off-by-one error cannot make
     * either half of the heartbeat shorter than the specified 500 ms. */
    assert(output_expander_heartbeat_level(0U));
    assert(output_expander_heartbeat_level(499U));
    assert(!output_expander_heartbeat_level(500U));
    assert(!output_expander_heartbeat_level(999U));
    assert(output_expander_heartbeat_level(1000U));
    return 0;
}
