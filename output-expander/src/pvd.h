#ifndef OUTPUT_EXPANDER_PVD_H
#define OUTPUT_EXPANDER_PVD_H

#include <stdbool.h>

/* Enable the CH32V003 programmable voltage detector at the 3.05 V rising /
 * 2.9 V falling threshold. This monitors the MCU's regulated 3.3 V VCC rail
 * while retaining margin above its 2.7 V minimum operating voltage. */
void pvd_init(void);

/* True once a low-voltage condition has occurred. The latch clears only on
 * reset, preventing foreground commands from re-energizing an unsafe rail. */
bool pvd_brownout_detected(void);

/* C handler called by the assembly interrupt entry. */
void pvd_irq_handler(void);

#endif
