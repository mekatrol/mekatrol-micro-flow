/* Host regression tests for supervisor queues, dispatch, timing, and faults. */
#include <microflow/supervisor.h>

#include <stdint.h>
#include <stdlib.h>

/*
 * These tests exercise the supervisor with a deterministic, in-memory platform
 * fixture.  No wall clock, interrupt controller, or board SDK is involved: the
 * test controls time explicitly and records every observable platform action.
 * This keeps the host test repeatable while checking the same boundary cases
 * that matter on the target, including bounded queues, timer wraparound,
 * priority dispatch, deferred fault reporting, and repeated warm starts.
 */

/* A failed expectation aborts the otherwise freestanding-style test program. */
#define CHECK(condition)           \
    do {                           \
        if (!(condition)) abort(); \
    } while (0)

/*
 * The fixture is both the platform context and the service context.  Counters
 * expose side effects without relying on stdout, while order[] captures the
 * event mask seen by each dispatched service.  The callbacks below deliberately
 * remain small so each assertion can attribute a state change to one operation.
 */
typedef struct {
    uint32_t now, waits, watchdogs, faults;
    bool     has_deadline;
    uint32_t deadline;
    uint8_t  order[8];
    size_t   order_count;
} fixture_t;

/* Supply deterministic test-controlled time to the supervisor platform. */
static uint32_t fixture_now(void *context) {
    /* Time advances only when the test or service callback changes it. */
    return ((fixture_t *)context)->now;
}

/* Record idle requests rather than sleeping the host test process. */
static void fixture_wait(void *context, bool has_deadline, uint32_t deadline) {
    fixture_t *f = context;
    /* Record, rather than perform, the idle wait requested by the supervisor. */
    ++f->waits;
    f->has_deadline = has_deadline;
    f->deadline     = deadline;
}

/* Count watchdog refreshes so every run-loop path can prove liveness service. */
static void fixture_watchdog(void *context) {
    /* One increment represents one platform watchdog service operation. */
    ++((fixture_t *)context)->watchdogs;
}

/* Record deferred platform fault reporting separately from fault detection. */
static void fixture_fault(void *context, microflow_supervisor_fault_t fault) {
    /*
     * These tests care when reporting occurs, not which platform diagnostic
     * encoding is used.  The supervisor's own fault field retains the reason.
     */
    (void)fault;
    ++((fixture_t *)context)->faults;
}

/* Record service order/event delivery and advance time to model bounded work. */
static void service(void *context, uint32_t events) {
    fixture_t *f = context;
    /* Preserve dispatch order/event delivery, then model one tick of work. */
    f->order[f->order_count++] = (uint8_t)events;
    ++f->now;
}

/* Assemble the complete fake platform contract used by each test scenario. */
static microflow_supervisor_platform_t platform(fixture_t *f) {
    /* Construct the complete fake platform used by each supervisor instance. */
    return (microflow_supervisor_platform_t){.now              = fixture_now,
                                             .wait_until       = fixture_wait,
                                             .watchdog_service = fixture_watchdog,
                                             .fault_report     = fixture_fault,
                                             .context          = f};
}

/* Prove bounded byte/event queues preserve unread FIFO data on overflow. */
static void test_queues_reject_newest(void) {
    /*
     * Purpose: prove both SPSC queue types have the documented bounded FIFO
     * policy.  A ring leaves one slot unused so equal read/write indices mean
     * "empty" unambiguously; therefore an array of N elements accepts N - 1.
     * When full, rejecting the newest item must preserve all unread data.
     */
    microflow_byte_ring_t bytes;
    microflow_byte_ring_initialize(&bytes);

    /* Fill every usable byte slot with a sequence that exposes reordering. */
    for (uint32_t i = 0U; i < MICROFLOW_SUPERVISOR_BYTE_CAPACITY - 1U; ++i) {
        CHECK(microflow_byte_ring_push_from_isr(&bytes, (uint8_t)i));
    }

    /*
     * The next push is expected to fail because the ring is full.  Exactly one
     * rejection and a high-water mark of N - 1 show that capacity accounting
     * is bounded and includes every successfully queued byte.
     */
    CHECK(!microflow_byte_ring_push_from_isr(&bytes, 0xAAU));
    CHECK(bytes.rejected == 1U && bytes.high_water == MICROFLOW_SUPERVISOR_BYTE_CAPACITY - 1U);

    /* FIFO output must contain the original sequence, never rejected 0xAA. */
    for (uint32_t i = 0U; i < MICROFLOW_SUPERVISOR_BYTE_CAPACITY - 1U; ++i) {
        uint8_t value = 0U;
        CHECK(microflow_byte_ring_pop(&bytes, &value));
        CHECK(value == (uint8_t)i);
    }

    /* Repeat the same policy check for the queue of structured events. */
    microflow_event_queue_t events;
    microflow_event_queue_initialize(&events);
    for (uint32_t i = 0U; i < MICROFLOW_SUPERVISOR_EVENT_CAPACITY - 1U; ++i) {
        CHECK(microflow_event_queue_push_from_isr(&events, (microflow_supervisor_event_t){i, i + 1U}));
    }

    /* The full event queue rejects one new event and records its peak usage. */
    CHECK(!microflow_event_queue_push_from_isr(&events, (microflow_supervisor_event_t){99U, 99U}));
    CHECK(events.rejected == 1U && events.high_water == MICROFLOW_SUPERVISOR_EVENT_CAPACITY - 1U);

    /* Checking both fields proves complete events remain paired and FIFO. */
    for (uint32_t i = 0U; i < MICROFLOW_SUPERVISOR_EVENT_CAPACITY - 1U; ++i) {
        microflow_supervisor_event_t event;
        CHECK(microflow_event_queue_pop(&events, &event));
        CHECK(event.kind == i && event.value == i + 1U);
    }
}

/* Prove priority dispatch, idle waiting, runtime metrics, and watchdog service. */
static void test_priority_metrics_idle_and_overrun(void) {
    /*
     * Purpose: verify idle behavior, priority sorting, dispatch metrics, and
     * watchdog servicing.  Time starts at 100 and the ten-tick heartbeat makes
     * 110 the first deadline, giving all expected values simple provenance.
     */
    fixture_t                       f = {.now = 100U};
    microflow_supervisor_t          s;
    microflow_supervisor_platform_t p = platform(&f);
    CHECK(microflow_supervisor_initialize(&s, &p, 10U));

    /*
     * Add the lower-priority service first on purpose.  Adding priority 1 after
     * priority 20 must insert it at index zero.  Both subscribe to USB, so one
     * signal should dispatch both.  A zero budget disables overrun checking for
     * the second service; the first has a two-tick budget and will use one.
     */
    CHECK(microflow_supervisor_add_service(&s, 20U, MICROFLOW_SUPERVISOR_EVENT_USB, 2U, service, &f));
    CHECK(microflow_supervisor_add_service(&s, 1U, MICROFLOW_SUPERVISOR_EVENT_USB, 0U, service, &f));

    /*
     * With no pending event and no elapsed heartbeat, run_once reports no work,
     * services the watchdog, and asks the platform to wait until tick 110.
     */
    CHECK(!microflow_supervisor_run_once(&s));
    CHECK(f.waits == 1U && f.has_deadline && f.deadline == 110U && f.watchdogs == 1U);

    /* Signal USB as an ISR would; tick 105 is still before the heartbeat. */
    microflow_supervisor_signal_from_isr(&s, MICROFLOW_SUPERVISOR_EVENT_USB);
    f.now = 105U;
    CHECK(microflow_supervisor_run_once(&s));

    /*
     * Both matching services ran, and the priority-1 entry was sorted first.
     * Each callback advances fake time once, so each measured maximum is one
     * tick.  Work was available, hence no second idle wait; every pass still
     * services the watchdog, increasing its count from one to two.
     */
    CHECK(f.order_count == 2U && s.services[0].priority == 1U);
    CHECK(s.services[0].observed_maximum_runtime == 1U && s.services[1].observed_maximum_runtime == 1U);
    CHECK(f.waits == 1U && f.watchdogs == 2U);
}

/* Prove clock wrap, deferred overflow faults, and clean repeated initialization. */
static void test_wraparound_warm_reset_and_fault(void) {
    /*
     * Purpose: cover three resilience properties relevant to long-lived
     * firmware: unsigned clock wraparound, ISR-safe deferred diagnostics, and
     * complete supervisor reinitialization after repeated warm resets.
     */
    fixture_t                       f = {.now = UINT32_MAX - 3U};
    microflow_supervisor_t          s;
    microflow_supervisor_platform_t p = platform(&f);
    CHECK(microflow_supervisor_initialize(&s, &p, 5U));
    CHECK(microflow_supervisor_add_service(&s, 0U, MICROFLOW_SUPERVISOR_EVENT_TIMER, 0U, service, &f));

    /*
     * The deadline wraps from UINT32_MAX - 3 plus five ticks to 1.  Setting now
     * to 1 means it has arrived, so run_once raises TIMER, dispatches the timer
     * service, and increments the heartbeat exactly once.
     */
    f.now = 1U;
    CHECK(microflow_supervisor_run_once(&s));
    CHECK(s.heartbeat == 1U);

    /* Fill a byte ring, then use the supervisor-aware wrapper for overflow. */
    microflow_byte_ring_t ring;
    microflow_byte_ring_initialize(&ring);
    for (uint32_t i = 0U; i < MICROFLOW_SUPERVISOR_BYTE_CAPACITY - 1U; ++i) {
        CHECK(microflow_supervisor_byte_ring_push_from_isr(&s, &ring, (uint8_t)i));
    }
    CHECK(!microflow_supervisor_byte_ring_push_from_isr(&s, &ring, 0U));

    /*
     * Overflow is recorded immediately in supervisor state, but the platform
     * reporter must not run in the ISR-facing push path.  Reporting is deferred
     * until run_once consumes the pending FAULT event in normal context.
     */
    CHECK(s.fault_count == 1U && f.faults == 0U);
    CHECK(microflow_supervisor_run_once(&s));
    CHECK(f.faults == 1U);

    /*
     * Reinitialize many times while time advances to simulate warm boots at
     * arbitrary clock values.  Runtime state must never leak across a reset:
     * heartbeat, faults, and pending events are expected to return to zero.
     */
    for (uint32_t reset = 0U; reset < 1000U; ++reset) {
        f.now += 17U;
        CHECK(microflow_supervisor_initialize(&s, &p, 5U));
        CHECK(s.heartbeat == 0U && s.fault_count == 0U && s.pending_events == 0U);
    }
}

/* Run the independent supervisor contract scenarios in one host executable. */
int main(void) {
    /* Keep each scenario independent so a failure identifies one contract. */
    test_queues_reject_newest();
    test_priority_metrics_idle_and_overrun();
    test_wraparound_warm_reset_and_fault();
    return 0;
}
