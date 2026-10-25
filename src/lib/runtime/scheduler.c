/* Allocation-free scheduler implementation for portable ISO C11 targets. */
#include <microflow/scheduler.h>

#include <limits.h>

/* Enforce the half-counter-range horizon required by wrap-safe time ordering. */
static bool microflow_scheduler_delay_is_valid(microflow_scheduler_tick_t delay) {
    /* Signed-difference time ordering is unambiguous only within half range. */
    return delay <= (microflow_scheduler_tick_t)INT32_MAX;
}

/* Determine deadline readiness correctly even when the uint32 clock wraps. */
static bool microflow_scheduler_time_has_arrived(
    microflow_scheduler_tick_t now,
    microflow_scheduler_tick_t deadline) {
    /* The cast deliberately interprets wrapped subtraction as relative time. */
    return (int32_t)(now - deadline) >= 0;
}

/* Verify cancellation targets caller storage assigned to this scheduler. */
static bool microflow_scheduler_owns_task(
    const microflow_scheduler_t *scheduler,
    const microflow_scheduler_task_t *task) {
    if ((scheduler == NULL) || (task == NULL) || (scheduler->tasks == NULL)) {
        return false;
    }

    /* Compare addresses instead of dereferencing a possibly foreign task. */
    for (size_t index = 0U; index < scheduler->capacity; ++index) {
        if (&scheduler->tasks[index] == task) {
            return true;
        }
    }

    return false;
}

/* Bind fixed task storage and clear all state for deterministic reuse. */
microflow_scheduler_result_t microflow_scheduler_initialize(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_task_t *task_storage,
    size_t task_capacity) {
    if ((scheduler == NULL) || ((task_capacity != 0U) && (task_storage == NULL))) {
        return MICROFLOW_SCHEDULER_INVALID_ARGUMENT;
    }

    scheduler->tasks = task_storage;
    scheduler->capacity = task_capacity;
    scheduler->dispatch_id = 0U;
    scheduler->dispatching = false;

    /* Zeroing public storage prevents stale callbacks surviving reinitialize. */
    for (size_t index = 0U; index < task_capacity; ++index) {
        task_storage[index] = (microflow_scheduler_task_t){0};
    }

    return MICROFLOW_SCHEDULER_OK;
}

/* Reserve one free slot and describe a one-shot or periodic future callback. */
microflow_scheduler_result_t microflow_scheduler_schedule(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_tick_t now,
    microflow_scheduler_tick_t delay,
    microflow_scheduler_tick_t period,
    uint8_t priority,
    microflow_scheduler_callback_t callback,
    void *context,
    microflow_scheduler_task_t **task_out) {
    if ((scheduler == NULL) || (callback == NULL) ||
        ((scheduler->capacity != 0U) && (scheduler->tasks == NULL))) {
        return MICROFLOW_SCHEDULER_INVALID_ARGUMENT;
    }

    /* Delay and period share the same wrap-safe comparison constraint. */
    if (!microflow_scheduler_delay_is_valid(delay) ||
        !microflow_scheduler_delay_is_valid(period)) {
        return MICROFLOW_SCHEDULER_INVALID_DELAY;
    }

    for (size_t index = 0U; index < scheduler->capacity; ++index) {
        microflow_scheduler_task_t *task = &scheduler->tasks[index];

        /* Running slots remain reserved even if their callback cancelled them. */
        if (task->active || task->running) {
            continue;
        }

        /* Deadline addition may wrap; later signed comparisons remain valid. */
        *task = (microflow_scheduler_task_t){
            .callback = callback,
            .context = context,
            .deadline = now + delay,
            .period = period,
            .priority = priority,
            .active = true,
        };

        if (task_out != NULL) {
            *task_out = task;
        }
        return MICROFLOW_SCHEDULER_OK;
    }

    return MICROFLOW_SCHEDULER_CAPACITY_EXHAUSTED;
}

/* Make an owned task ineligible for future dispatch without freeing memory. */
microflow_scheduler_result_t microflow_scheduler_cancel(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_task_t *task) {
    if ((scheduler == NULL) || (task == NULL)) {
        return MICROFLOW_SCHEDULER_INVALID_ARGUMENT;
    }
    if (!microflow_scheduler_owns_task(scheduler, task)) {
        return MICROFLOW_SCHEDULER_TASK_NOT_OWNED;
    }

    task->active = false;
    return MICROFLOW_SCHEDULER_OK;
}

/* Assign a unique pass identity so newly scheduled work cannot run repeatedly. */
static void microflow_scheduler_begin_dispatch(microflow_scheduler_t *scheduler) {
    /* A new identity ensures each ready task can run at most once this pass. */
    /* Zero is reserved as the cleared marker; scrub collisions after wrap. */
    ++scheduler->dispatch_id;
    if (scheduler->dispatch_id != 0U) {
        return;
    }

    ++scheduler->dispatch_id;
    for (size_t index = 0U; index < scheduler->capacity; ++index) {
        scheduler->tasks[index].dispatch_id = 0U;
    }
}

/* Select the highest-priority ready task not yet run in the current pass. */
static microflow_scheduler_task_t *microflow_scheduler_find_next_ready(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_tick_t now) {
    microflow_scheduler_task_t *selected = NULL;

    for (size_t index = 0U; index < scheduler->capacity; ++index) {
        microflow_scheduler_task_t *task = &scheduler->tasks[index];

        /* Exclude inactive, recursive, already-run, and future tasks. */
        if (!task->active || task->running ||
            (task->dispatch_id == scheduler->dispatch_id) ||
            !microflow_scheduler_time_has_arrived(now, task->deadline)) {
            continue;
        }

        /* Strict comparison preserves storage order for equal priorities. */
        if ((selected == NULL) || (task->priority < selected->priority)) {
            selected = task;
        }
    }

    return selected;
}

/* Advance a periodic release beyond now without drift or catch-up bursts. */
static void microflow_scheduler_advance_periodic_task(
    microflow_scheduler_task_t *task,
    microflow_scheduler_tick_t now) {
    /* Advance from the old release, skipping backlog without schedule drift. */
    const microflow_scheduler_tick_t elapsed = now - task->deadline;
    const microflow_scheduler_tick_t releases_missed = elapsed / task->period;

    task->deadline += (releases_missed + 1U) * task->period;
}

/* Run the ready snapshot once in deterministic priority and storage order. */
size_t microflow_scheduler_run_ready(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_tick_t now) {
    /* Invalid state and recursive dispatch both fail without side effects. */
    if ((scheduler == NULL) || scheduler->dispatching ||
        ((scheduler->capacity != 0U) && (scheduler->tasks == NULL))) {
        return 0U;
    }

    scheduler->dispatching = true;
    microflow_scheduler_begin_dispatch(scheduler);

    size_t tasks_run = 0U;
    microflow_scheduler_task_t *task =
        microflow_scheduler_find_next_ready(scheduler, now);

    while (task != NULL) {
        /* Mark before the callback because callbacks may mutate scheduler state. */
        task->dispatch_id = scheduler->dispatch_id;
        task->running = true;

        if (task->period == 0U) {
            /* Release one-shot storage before calling back so it may reschedule. */
            task->active = false;
        } else {
            microflow_scheduler_advance_periodic_task(task, now);
        }

        task->callback(scheduler, task, task->context);
        task->running = false;
        ++tasks_run;

        task = microflow_scheduler_find_next_ready(scheduler, now);
    }

    scheduler->dispatching = false;
    return tasks_run;
}

/* Find the earliest active deadline so a platform can sleep until required. */
bool microflow_scheduler_next_deadline(
    const microflow_scheduler_t *scheduler,
    microflow_scheduler_tick_t *deadline_out) {
    if ((scheduler == NULL) || (deadline_out == NULL) ||
        ((scheduler->capacity != 0U) && (scheduler->tasks == NULL))) {
        return false;
    }

    const microflow_scheduler_task_t *selected = NULL;
    for (size_t index = 0U; index < scheduler->capacity; ++index) {
        const microflow_scheduler_task_t *task = &scheduler->tasks[index];
        if (!task->active) {
            continue;
        }

        /* Signed ordering is valid within the documented half-range horizon. */
        if ((selected == NULL) ||
            ((int32_t)(task->deadline - selected->deadline) < 0)) {
            selected = task;
        }
    }

    if (selected == NULL) {
        return false;
    }

    *deadline_out = selected->deadline;
    return true;
}
