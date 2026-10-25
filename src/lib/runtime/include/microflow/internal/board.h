/* Internal portable-to-board contract; implementations remain board-owned. */
#ifndef MICROFLOW_INTERNAL_BOARD_H
#define MICROFLOW_INTERNAL_BOARD_H

#include <stdint.h>

/* Initializes all board facilities required by the portable runtime. */
void microflow_board_initialize(void);
/* Returns wrapping, monotonic board ticks in the board's documented unit. */
uint32_t microflow_board_monotonic_ticks(void);
/* Presents runtime state through the board's available status indicator. */
void microflow_board_status_rgb(uint8_t red, uint8_t green, uint8_t blue);
/* Performs a board delay used only where deliberate blocking is acceptable. */
void microflow_board_delay_ms(uint32_t milliseconds);

#endif
