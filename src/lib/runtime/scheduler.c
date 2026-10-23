#include <microflow/scheduler.h>

#include <limits.h>

static bool microflow_scheduler_delay_is_valid(microflow_scheduler_tick_t delay) {
    return delay <= (microflow_scheduler_tick_t)INT32_MAX;
}

static bool microflow_scheduler_time_has_arrived(
    microflow_scheduler_tick_t now,
    microflow_scheduler_tick_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static bool microflow_scheduler_owns_task(
    const microflow_scheduler_t *scheduler,
    const microflow_scheduler_task_t *task) {
    if ((scheduler == NULL) || (task == NULL) || (scheduler->tasks == NULL)) {
        return false;
    }

    for (size_t index = 0U; index < scheduler->capacity; ++index) {
        if (&scheduler->tasks[index] == task) {
            return true;
        }
    }

    return false;
}

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

    for (size_t index = 0U; index < task_capacity; ++index) {
        task_storage[index] = (microflow_scheduler_task_t){0};
    }

    return MICROFLOW_SCHEDULER_OK;
}

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

    if (!microflow_scheduler_delay_is_valid(delay) ||
        !microflow_scheduler_delay_is_valid(period)) {
        return MICROFLOW_SCHEDULER_INVALID_DELAY;
    }

    for (size_t index = 0U; index < scheduler->capacity; ++index) {
        microflow_scheduler_task_t *task = &scheduler->tasks[index];

        if (task->active || task->running) {
            continue;
        }

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

static void microflow_scheduler_begin_dispatch(microflow_scheduler_t *scheduler) {
    ++scheduler->dispatch_id;
    if (scheduler->dispatch_id != 0U) {
        return;
    }

    ++scheduler->dispatch_id;
    for (size_t index = 0U; index < scheduler->capacity; ++index) {
        scheduler->tasks[index].dispatch_id = 0U;
    }
}

static microflow_scheduler_task_t *microflow_scheduler_find_next_ready(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_tick_t now) {
    microflow_scheduler_task_t *selected = NULL;

    for (size_t index = 0U; index < scheduler->capacity; ++index) {
        microflow_scheduler_task_t *task = &scheduler->tasks[index];

        if (!task->active || task->running ||
            (task->dispatch_id == scheduler->dispatch_id) ||
            !microflow_scheduler_time_has_arrived(now, task->deadline)) {
            continue;
        }

        if ((selected == NULL) || (task->priority < selected->priority)) {
            selected = task;
        }
    }

    return selected;
}

static void microflow_scheduler_advance_periodic_task(
    microflow_scheduler_task_t *task,
    microflow_scheduler_tick_t now) {
    const microflow_scheduler_tick_t elapsed = now - task->deadline;
    const microflow_scheduler_tick_t releases_missed = elapsed / task->period;

    task->deadline += (releases_missed + 1U) * task->period;
}

size_t microflow_scheduler_run_ready(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_tick_t now) {
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
        task->dispatch_id = scheduler->dispatch_id;
        task->running = true;

        if (task->period == 0U) {
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
