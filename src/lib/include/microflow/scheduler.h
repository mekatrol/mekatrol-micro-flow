/* Portable, allocation-free cooperative scheduler public interface. */
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

typedef struct microflow_scheduler      microflow_scheduler_t;
typedef struct microflow_scheduler_task microflow_scheduler_task_t;

typedef void (*microflow_scheduler_callback_t)(
    microflow_scheduler_t      *scheduler,
    microflow_scheduler_task_t *task,
    void                       *context);

typedef enum {
    /* Operation completed and any requested state transition took effect. */
    MICROFLOW_SCHEDULER_OK = 0,
    /* A required pointer or scheduler/storage relationship was invalid. */
    MICROFLOW_SCHEDULER_INVALID_ARGUMENT,
    /* Every caller-provided task slot is active or currently executing. */
    MICROFLOW_SCHEDULER_CAPACITY_EXHAUSTED,
    /* Delay or period exceeds the wrap-safe INT32_MAX-tick horizon. */
    MICROFLOW_SCHEDULER_INVALID_DELAY,
    /* Cancellation targeted storage outside this scheduler's task array. */
    MICROFLOW_SCHEDULER_TASK_NOT_OWNED
} microflow_scheduler_result_t;

/*
 * Task and scheduler storage are public so callers can allocate them
 * statically. Their fields are implementation state and must not be changed
 * directly after microflow_scheduler_initialize().
 */
struct microflow_scheduler_task {
    /* Function and opaque caller state used when the deadline becomes ready. */
    microflow_scheduler_callback_t callback;
    void                          *context;
    /* Absolute next release and zero-for-one-shot recurrence interval. */
    microflow_scheduler_tick_t deadline;
    microflow_scheduler_tick_t period;
    /* Pass marker prevents a task from running twice in one ready scan. */
    uint32_t dispatch_id;
    /* Smaller values run first when multiple deadlines are ready. */
    uint8_t priority;
    /* Active means scheduled; running protects a callback's occupied slot. */
    bool active;
    bool running;
};

struct microflow_scheduler {
    /* Caller-owned fixed array avoids allocation and makes memory measurable. */
    microflow_scheduler_task_t *tasks;
    size_t                      capacity;
    /* Monotonic pass identity, with explicit handling when it wraps to zero. */
    uint32_t dispatch_id;
    /* Rejects recursive run_ready calls from task callbacks. */
    bool dispatching;
};

/* Associates caller storage and clears every task slot to an inactive state. */
microflow_scheduler_result_t microflow_scheduler_initialize(
    microflow_scheduler_t      *scheduler,
    microflow_scheduler_task_t *task_storage,
    size_t                      task_capacity);

/* Lower numeric values run first. Equal priorities retain storage order. */
microflow_scheduler_result_t microflow_scheduler_schedule(
    microflow_scheduler_t         *scheduler,
    microflow_scheduler_tick_t     now,
    microflow_scheduler_tick_t     delay,
    microflow_scheduler_tick_t     period,
    uint8_t                        priority,
    microflow_scheduler_callback_t callback,
    void                          *context,
    microflow_scheduler_task_t   **task_out);

/* Cancels an owned task; a currently running callback is allowed to finish. */
microflow_scheduler_result_t microflow_scheduler_cancel(
    microflow_scheduler_t      *scheduler,
    microflow_scheduler_task_t *task);

/*
 * Runs each task that was ready at most once. Periodic deadlines advance from
 * their prior deadline, skipping missed releases without drift or catch-up
 * bursts. Callbacks run to completion and must remain short and bounded.
 */
size_t microflow_scheduler_run_ready(
    microflow_scheduler_t     *scheduler,
    microflow_scheduler_tick_t now);

/* Finds the earliest active absolute deadline, or false when none exists. */
bool microflow_scheduler_next_deadline(
    const microflow_scheduler_t *scheduler,
    microflow_scheduler_tick_t  *deadline_out);

#ifdef __cplusplus
}
#endif

#endif
