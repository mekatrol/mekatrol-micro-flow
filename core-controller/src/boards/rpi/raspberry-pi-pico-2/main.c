/*
 * RP2350 board placeholder.
 *
 * The Pico 2 target is intentionally non-functional until its startup,
 * linker, peripheral, and flow-engine integration satisfy the repository's
 * support criteria.  Keeping a minimal entry point lets the partial target be
 * compiled without pretending that hardware initialization is complete.
 */
/* Hold the incomplete Pico 2 target safely instead of returning from firmware. */
int main(void) {
    /* Never return into an undefined embedded runtime exit path. */
    for (;;) {
        /* No services exist for this partial target yet. */
    }
}
