#include "supervisor.h"

#include <stdint.h>
#include <stdlib.h>

#define CHECK(condition)           \
    do {                           \
        if (!(condition)) abort(); \
    } while (0)

typedef struct {
    uint32_t now, waits, watchdogs, faults;
    bool     has_deadline;
    uint32_t deadline;
    uint8_t  order[8];
    size_t   order_count;
} fixture_t;

static uint32_t fixture_now(void *context) {
    return ((fixture_t *)context)->now;
}

static void fixture_wait(void *context, bool has_deadline, uint32_t deadline) {
    fixture_t *f = context;
    ++f->waits;
    f->has_deadline = has_deadline;
    f->deadline     = deadline;
}

static void fixture_watchdog(void *context) {
    ++((fixture_t *)context)->watchdogs;
}

static void fixture_fault(void *context, microflow_supervisor_fault_t fault) {
    (void)fault;
    ++((fixture_t *)context)->faults;
}

static void service(void *context, uint32_t events) {
    fixture_t *f               = context;
    f->order[f->order_count++] = (uint8_t)events;
    ++f->now;
}

static microflow_supervisor_platform_t platform(fixture_t *f) {
    return (microflow_supervisor_platform_t){.now              = fixture_now,
                                             .wait_until       = fixture_wait,
                                             .watchdog_service = fixture_watchdog,
                                             .fault_report     = fixture_fault,
                                             .context          = f};
}

static void test_queues_reject_newest(void) {
    microflow_byte_ring_t bytes;
    microflow_byte_ring_initialize(&bytes);
    for (uint32_t i = 0U; i < MICROFLOW_SUPERVISOR_BYTE_CAPACITY - 1U; ++i)
        CHECK(microflow_byte_ring_push_from_isr(&bytes, (uint8_t)i));
    CHECK(!microflow_byte_ring_push_from_isr(&bytes, 0xAAU));
    CHECK(bytes.rejected == 1U && bytes.high_water == MICROFLOW_SUPERVISOR_BYTE_CAPACITY - 1U);
    for (uint32_t i = 0U; i < MICROFLOW_SUPERVISOR_BYTE_CAPACITY - 1U; ++i) {
        uint8_t value = 0U;
        CHECK(microflow_byte_ring_pop(&bytes, &value));
        CHECK(value == (uint8_t)i);
    }
    microflow_event_queue_t events;
    microflow_event_queue_initialize(&events);
    for (uint32_t i = 0U; i < MICROFLOW_SUPERVISOR_EVENT_CAPACITY - 1U; ++i)
        CHECK(microflow_event_queue_push_from_isr(&events, (microflow_supervisor_event_t){i, i + 1U}));
    CHECK(!microflow_event_queue_push_from_isr(&events, (microflow_supervisor_event_t){99U, 99U}));
    CHECK(events.rejected == 1U && events.high_water == MICROFLOW_SUPERVISOR_EVENT_CAPACITY - 1U);
    for (uint32_t i = 0U; i < MICROFLOW_SUPERVISOR_EVENT_CAPACITY - 1U; ++i) {
        microflow_supervisor_event_t event;
        CHECK(microflow_event_queue_pop(&events, &event));
        CHECK(event.kind == i && event.value == i + 1U);
    }
}

static void test_priority_metrics_idle_and_overrun(void) {
    fixture_t                       f = {.now = 100U};
    microflow_supervisor_t          s;
    microflow_supervisor_platform_t p = platform(&f);
    CHECK(microflow_supervisor_initialize(&s, &p, 10U));
    CHECK(microflow_supervisor_add_service(&s, 20U, MICROFLOW_SUPERVISOR_EVENT_USB, 2U, service, &f));
    CHECK(microflow_supervisor_add_service(&s, 1U, MICROFLOW_SUPERVISOR_EVENT_USB, 0U, service, &f));
    CHECK(!microflow_supervisor_run_once(&s));
    CHECK(f.waits == 1U && f.has_deadline && f.deadline == 110U && f.watchdogs == 1U);
    microflow_supervisor_signal_from_isr(&s, MICROFLOW_SUPERVISOR_EVENT_USB);
    f.now = 105U;
    CHECK(microflow_supervisor_run_once(&s));
    CHECK(f.order_count == 2U && s.services[0].priority == 1U);
    CHECK(s.services[0].observed_maximum_runtime == 1U && s.services[1].observed_maximum_runtime == 1U);
    CHECK(f.waits == 1U && f.watchdogs == 2U);
}

static void test_wraparound_warm_reset_and_fault(void) {
    fixture_t                       f = {.now = UINT32_MAX - 3U};
    microflow_supervisor_t          s;
    microflow_supervisor_platform_t p = platform(&f);
    CHECK(microflow_supervisor_initialize(&s, &p, 5U));
    CHECK(microflow_supervisor_add_service(&s, 0U, MICROFLOW_SUPERVISOR_EVENT_TIMER, 0U, service, &f));
    f.now = 1U;
    CHECK(microflow_supervisor_run_once(&s));
    CHECK(s.heartbeat == 1U);
    microflow_byte_ring_t ring;
    microflow_byte_ring_initialize(&ring);
    for (uint32_t i = 0U; i < MICROFLOW_SUPERVISOR_BYTE_CAPACITY - 1U; ++i)
        CHECK(microflow_supervisor_byte_ring_push_from_isr(&s, &ring, (uint8_t)i));
    CHECK(!microflow_supervisor_byte_ring_push_from_isr(&s, &ring, 0U));
    CHECK(s.fault_count == 1U && f.faults == 0U);
    CHECK(microflow_supervisor_run_once(&s));
    CHECK(f.faults == 1U);
    for (uint32_t reset = 0U; reset < 1000U; ++reset) {
        f.now += 17U;
        CHECK(microflow_supervisor_initialize(&s, &p, 5U));
        CHECK(s.heartbeat == 0U && s.fault_count == 0U && s.pending_events == 0U);
    }
}

int main(void) {
    test_queues_reject_newest();
    test_priority_metrics_idle_and_overrun();
    test_wraparound_warm_reset_and_fault();
    return 0;
}
