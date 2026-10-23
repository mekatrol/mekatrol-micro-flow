#include <microflow/internal/board.h>

int main(void) {
  microflow_board_initialize();

  for (;;) {
    microflow_board_status_rgb(16U, 0U, 0U);
    microflow_board_delay_ms(500U);
    microflow_board_status_rgb(0U, 16U, 0U);
    microflow_board_delay_ms(500U);
    microflow_board_status_rgb(0U, 0U, 16U);
    microflow_board_delay_ms(500U);
    microflow_board_status_rgb(0U, 0U, 0U);
    microflow_board_delay_ms(500U);
  }
}
