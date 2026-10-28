#include "ch32v003.h"

int main(void)
{
    /* Configure the clock and put every externally connected signal into its
     * safe startup state before the application begins changing an output. */
    ch32v003_init();

    /* PC0 drives output channel 1. Waiting 500 ms between transitions gives
     * one visible ON/OFF cycle per second without requiring interrupts during
     * the minimal Phase 0 hardware test. */
    for (;;) {
        ch32v003_delay_half_second();
        ch32v003_toggle_output0();
    }
}
