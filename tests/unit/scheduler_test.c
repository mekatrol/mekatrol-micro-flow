/* Host regression tests for deterministic portable scheduler semantics. */
#include <microflow/scheduler.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            abort();                                                           \
        }                                                                      \
    } while (0)

typedef struct {
    uint8_t values[16];
    size_t count;
} event_log_t;

typedef struct {
    event_log_t *log;
    uint8_t value;
} callback_context_t;

/* Record callback order as compact values so priority behavior is observable. */
static void record_callback(
    microflow_scheduler_t *scheduler,
    microflow_scheduler_task_t *task,
    void *context) {
    (void)scheduler;
    (void)task;
    callback_context_t *callback_context = context;
    callback_context->log->values[callback_context->log->count] =
        callback_context->value;
    ++callback_context->log->count;
}

/* Prove deadline gating, priority ordering, and stable equal-priority order. */
static void test_priority_deadlines_and_stable_order(void) {
    /* Ready tasks sort by priority while ties preserve task-storage order. */
    microflow_scheduler_t scheduler;
    microflow_scheduler_task_t tasks[4];
    event_log_t log = {0};
    callback_context_t low = {.log = &log, .value = 3U};
    callback_context_t high_first = {.log = &log, .value = 1U};
    callback_context_t high_second = {.log = &log, .value = 2U};

    CHECK(microflow_scheduler_initialize(&scheduler, tasks, 4U) ==
           MICROFLOW_SCHEDULER_OK);
    CHECK(microflow_scheduler_schedule(
               &scheduler, 100U, 5U, 0U, 10U, record_callback, &low, NULL) ==
           MICROFLOW_SCHEDULER_OK);
    CHECK(microflow_scheduler_schedule(
               &scheduler, 100U, 5U, 0U, 1U, record_callback, &high_first,
               NULL) == MICROFLOW_SCHEDULER_OK);
    CHECK(microflow_scheduler_schedule(
               &scheduler, 100U, 5U, 0U, 1U, record_callback, &high_second,
               NULL) == MICROFLOW_SCHEDULER_OK);

    CHECK(microflow_scheduler_run_ready(&scheduler, 104U) == 0U);
    CHECK(microflow_scheduler_run_ready(&scheduler, 105U) == 3U);
    CHECK(log.count == 3U);
    CHECK(log.values[0] == 1U);
    CHECK(log.values[1] == 2U);
    CHECK(log.values[2] == 3U);
}

/* Prove late periodic dispatch skips backlog without shifting future phase. */
static void test_periodic_skip_without_drift(void) {
    /* Late periodic work skips missed releases but retains its original phase. */
    microflow_scheduler_t scheduler;
    microflow_scheduler_task_t tasks[1];
    microflow_scheduler_task_t *periodic = NULL;
    event_log_t log = {0};
    callback_context_t context = {.log = &log, .value = 1U};

    CHECK(microflow_scheduler_initialize(&scheduler, tasks, 1U) ==
           MICROFLOW_SCHEDULER_OK);
    CHECK(microflow_scheduler_schedule(
               &scheduler, 10U, 5U, 10U, 0U, record_callback, &context,
               &periodic) == MICROFLOW_SCHEDULER_OK);

    CHECK(microflow_scheduler_run_ready(&scheduler, 46U) == 1U);
    CHECK(periodic->deadline == 55U);
    CHECK(microflow_scheduler_run_ready(&scheduler, 54U) == 0U);
    CHECK(microflow_scheduler_run_ready(&scheduler, 55U) == 1U);
    CHECK(periodic->deadline == 65U);
}

/* Prove wrap-safe readiness, cancellation, and deterministic capacity errors. */
static void test_wraparound_cancel_and_capacity(void) {
    /* Cover uint32 wrap, cancellation, and caller-supplied storage exhaustion. */
    microflow_scheduler_t scheduler;
    microflow_scheduler_task_t tasks[1];
    microflow_scheduler_task_t *task = NULL;
    event_log_t log = {0};
    callback_context_t context = {.log = &log, .value = 1U};

    CHECK(microflow_scheduler_initialize(&scheduler, tasks, 1U) ==
           MICROFLOW_SCHEDULER_OK);
    CHECK(microflow_scheduler_schedule(
               &scheduler, UINT32_MAX - 2U, 5U, 0U, 0U, record_callback,
               &context, &task) == MICROFLOW_SCHEDULER_OK);
    CHECK(microflow_scheduler_run_ready(&scheduler, 1U) == 0U);
    CHECK(microflow_scheduler_run_ready(&scheduler, 2U) == 1U);

    CHECK(microflow_scheduler_schedule(
               &scheduler, 2U, 0U, 0U, 0U, record_callback, &context,
               &task) == MICROFLOW_SCHEDULER_OK);
    CHECK(microflow_scheduler_schedule(
               &scheduler, 2U, 0U, 0U, 0U, record_callback, &context,
               NULL) == MICROFLOW_SCHEDULER_CAPACITY_EXHAUSTED);
    CHECK(microflow_scheduler_cancel(&scheduler, task) ==
           MICROFLOW_SCHEDULER_OK);
    CHECK(microflow_scheduler_run_ready(&scheduler, 2U) == 0U);
}

/* Prove invalid horizons fail and sleep planning selects the earliest release. */
static void test_argument_validation_and_next_deadline(void) {
    /* Reject ambiguous horizons and identify the earliest active deadline. */
    microflow_scheduler_t scheduler;
    microflow_scheduler_task_t tasks[2];
    microflow_scheduler_tick_t deadline = 0U;
    event_log_t log = {0};
    callback_context_t context = {.log = &log, .value = 1U};

    CHECK(microflow_scheduler_initialize(&scheduler, tasks, 2U) ==
           MICROFLOW_SCHEDULER_OK);
    CHECK(!microflow_scheduler_next_deadline(&scheduler, &deadline));
    CHECK(microflow_scheduler_schedule(
               &scheduler, 20U, (uint32_t)INT32_MAX + 1U, 0U, 0U,
               record_callback, &context, NULL) ==
           MICROFLOW_SCHEDULER_INVALID_DELAY);
    CHECK(microflow_scheduler_schedule(
               &scheduler, 20U, 8U, 0U, 0U, record_callback, &context,
               NULL) == MICROFLOW_SCHEDULER_OK);
    CHECK(microflow_scheduler_schedule(
               &scheduler, 20U, 3U, 0U, 0U, record_callback, &context,
               NULL) == MICROFLOW_SCHEDULER_OK);
    CHECK(microflow_scheduler_next_deadline(&scheduler, &deadline));
    CHECK(deadline == 23U);
}

/* Run independent scheduler contract scenarios as one host regression binary. */
int main(void) {
    test_priority_deadlines_and_stable_order();
    test_periodic_skip_without_drift();
    test_wraparound_cancel_and_capacity();
    test_argument_validation_and_next_deadline();
    return 0;
}
