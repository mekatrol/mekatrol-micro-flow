#ifndef MICROFLOW_INTERNAL_BOARD_H
#define MICROFLOW_INTERNAL_BOARD_H

#include <stdint.h>

void microflow_board_initialize(void);
void microflow_board_status_rgb(uint8_t red, uint8_t green, uint8_t blue);
void microflow_board_delay_ms(uint32_t milliseconds);

#endif
