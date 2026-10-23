#include <microflow/internal/board.h>
#include <microflow/scheduler.h>

#include <stddef.h>
#include <stdint.h>

#define STATUS_PERIOD_MICROSECONDS 500000U

static void status_task(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_task_t *task,
    void *context) {
  static const uint8_t colours[][3] = {
      {16U, 0U, 0U},
      {0U, 16U, 0U},
      {0U, 0U, 16U},
      {0U, 0U, 0U},
  };
  static size_t colour_index;

  (void)scheduler;
  (void)task;
  (void)context;

  microflow_board_status_rgb(
      colours[colour_index][0],
      colours[colour_index][1],
      colours[colour_index][2]);
  colour_index = (colour_index + 1U) % (sizeof(colours) / sizeof(colours[0]));
}

int main(void) {
  static microflow_scheduler_task_t task_storage[1];
  microflow_scheduler_t scheduler;

  microflow_board_initialize();

  if (microflow_scheduler_initialize(&scheduler, task_storage, 1U) !=
      MICROFLOW_SCHEDULER_OK) {
    return 1;
  }

  if (microflow_scheduler_schedule(
          &scheduler,
          microflow_board_monotonic_ticks(),
          0U,
          STATUS_PERIOD_MICROSECONDS,
          0U,
          status_task,
          NULL,
          NULL) != MICROFLOW_SCHEDULER_OK) {
    return 1;
  }

  for (;;) {
    (void)microflow_scheduler_run_ready(
        &scheduler, microflow_board_monotonic_ticks());
  }
}
