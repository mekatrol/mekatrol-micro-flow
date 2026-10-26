/* Internal portable-to-board contract; implementations remain board-owned. */
#ifndef MICROFLOW_INTERNAL_BOARD_H
#define MICROFLOW_INTERNAL_BOARD_H

#include <stdint.h>

/* Initializes all board facilities required by the portable runtime. */
void microflow_board_initialize(void);
/* Present runtime state through the available indicator so diagnostics survive transport failure. */
void microflow_board_status_rgb(uint8_t red, uint8_t green, uint8_t blue);
/* Performs a board delay used only where deliberate blocking is acceptable. */
void microflow_board_delay_ms(uint32_t milliseconds);

#endif
