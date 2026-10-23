#ifndef MICROFLOW_SCHEDULER_H
#define MICROFLOW_SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Scheduler time is an application-defined monotonic uint32_t tick count.
 * A board may use microseconds, while a smaller application may use
 * milliseconds. All deadlines must be no more than INT32_MAX ticks away.
 */
typedef uint32_t microflow_scheduler_tick_t;

typedef struct microflow_scheduler microflow_scheduler_t;
typedef struct microflow_scheduler_task microflow_scheduler_task_t;

typedef void (*microflow_scheduler_callback_t)(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_task_t *task,
    void *context);

typedef enum {
    MICROFLOW_SCHEDULER_OK = 0,
    MICROFLOW_SCHEDULER_INVALID_ARGUMENT,
    MICROFLOW_SCHEDULER_CAPACITY_EXHAUSTED,
    MICROFLOW_SCHEDULER_INVALID_DELAY,
    MICROFLOW_SCHEDULER_TASK_NOT_OWNED
} microflow_scheduler_result_t;

/*
 * Task and scheduler storage are public so callers can allocate them
 * statically. Their fields are implementation state and must not be changed
 * directly after microflow_scheduler_initialize().
 */
struct microflow_scheduler_task {
    microflow_scheduler_callback_t callback;
    void *context;
    microflow_scheduler_tick_t deadline;
    microflow_scheduler_tick_t period;
    uint32_t dispatch_id;
    uint8_t priority;
    bool active;
    bool running;
};

struct microflow_scheduler {
    microflow_scheduler_task_t *tasks;
    size_t capacity;
    uint32_t dispatch_id;
    bool dispatching;
};

microflow_scheduler_result_t microflow_scheduler_initialize(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_task_t *task_storage,
    size_t task_capacity);

/* Lower numeric values run first. Equal priorities retain storage order. */
microflow_scheduler_result_t microflow_scheduler_schedule(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_tick_t now,
    microflow_scheduler_tick_t delay,
    microflow_scheduler_tick_t period,
    uint8_t priority,
    microflow_scheduler_callback_t callback,
    void *context,
    microflow_scheduler_task_t **task_out);

microflow_scheduler_result_t microflow_scheduler_cancel(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_task_t *task);

/*
 * Runs each task that was ready at most once. Periodic deadlines advance from
 * their prior deadline, skipping missed releases without drift or catch-up
 * bursts. Callbacks run to completion and must remain short and bounded.
 */
size_t microflow_scheduler_run_ready(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_tick_t now);

bool microflow_scheduler_next_deadline(
    const microflow_scheduler_t *scheduler,
    microflow_scheduler_tick_t *deadline_out);

#ifdef __cplusplus
}
#endif

#endif
