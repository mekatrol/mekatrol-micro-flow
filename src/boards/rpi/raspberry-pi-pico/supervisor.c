/* Implements deterministic cooperative dispatch and bounded SPSC queues. */
#include "supervisor.h"
#include <limits.h>

/* Compute the next byte-ring slot while preserving the fixed storage bound. */
static uint16_t byte_next(uint16_t i) {
    return (uint16_t)((i + 1U) % MICROFLOW_SUPERVISOR_BYTE_CAPACITY);
}

/* Reset a byte ring and its operational metrics for initial use or warm reset.
 */
void microflow_byte_ring_initialize(microflow_byte_ring_t *r) {
    if (r != NULL) {
        r->read_index  = 0U;
        r->write_index = 0U;
        r->high_water  = 0U;
        r->rejected    = 0U;
    }
}

/* Report current occupancy for diagnostics and all-or-nothing reply checks. */
size_t microflow_byte_ring_count(const microflow_byte_ring_t *r) {
    return r == NULL ? 0U
                     : (r->write_index + MICROFLOW_SUPERVISOR_BYTE_CAPACITY -
                        r->read_index) %
                           MICROFLOW_SUPERVISOR_BYTE_CAPACITY;
}

/* Queue one ISR-produced byte without overwriting data the consumer has not
 * read. */
bool microflow_byte_ring_push_from_isr(microflow_byte_ring_t *r,
                                       uint8_t                value) {
    if (r == NULL) {
        return false;
    }
    /* One unused slot distinguishes full from empty using only two indices. */
    const uint16_t next = byte_next(r->write_index);
    if (next == r->read_index) {
        ++r->rejected;
        return false;
    }
    /* Publish data before advancing the producer-owned write index. */
    r->storage[r->write_index] = value;
    r->write_index             = next;
    const size_t count         = microflow_byte_ring_count(r);
    if (count > r->high_water) {
        r->high_water = (uint16_t)count;
    }
    return true;
}

/* Queue a byte and make overflow visible as a deferred supervisor fault. */
bool microflow_supervisor_byte_ring_push_from_isr(microflow_supervisor_t *s,
                                                  microflow_byte_ring_t  *r,
                                                  uint8_t                 value) {
    if (microflow_byte_ring_push_from_isr(r, value)) {
        return true;
    }
    /* Convert queue exhaustion into a deferred, observable system fault. */
    if (s != NULL) {
        s->fault = MICROFLOW_SUPERVISOR_FAULT_BYTE_QUEUE_OVERFLOW;
        ++s->fault_count;
        s->pending_events |= MICROFLOW_SUPERVISOR_EVENT_FAULT;
    }
    return false;
}

/* Remove the oldest byte for the sole main-context consumer without blocking.
 */
bool microflow_byte_ring_pop(microflow_byte_ring_t *r, uint8_t *out) {
    if ((r == NULL) || (out == NULL) || (r->read_index == r->write_index)) {
        return false;
    }
    *out          = r->storage[r->read_index];
    r->read_index = byte_next(r->read_index);
    return true;
}

/* Compute the next structured-event slot within its smaller fixed ring. */
static uint8_t event_next(uint8_t i) {
    return (uint8_t)((i + 1U) % MICROFLOW_SUPERVISOR_EVENT_CAPACITY);
}

/* Reset a structured event queue and all overflow/high-water diagnostics. */
void microflow_event_queue_initialize(microflow_event_queue_t *q) {
    if (q != NULL) {
        q->read_index  = 0U;
        q->write_index = 0U;
        q->high_water  = 0U;
        q->rejected    = 0U;
    }
}

/* Preserve and queue one complete ISR event, rejecting newest when full. */
bool microflow_event_queue_push_from_isr(microflow_event_queue_t     *q,
                                         microflow_supervisor_event_t event) {
    if (q == NULL) {
        return false;
    }
    const uint8_t next = event_next(q->write_index);
    /* Preserve the unread event set by rejecting the newest event on full. */
    if (next == q->read_index) {
        ++q->rejected;
        return false;
    }
    q->storage[q->write_index] = event;
    q->write_index             = next;
    const uint8_t count =
        (uint8_t)((q->write_index + MICROFLOW_SUPERVISOR_EVENT_CAPACITY -
                   q->read_index) %
                  MICROFLOW_SUPERVISOR_EVENT_CAPACITY);
    if (count > q->high_water) {
        q->high_water = count;
    }
    return true;
}

/* Queue a structured event and defer an observable overflow fault on failure.
 */
bool microflow_supervisor_event_queue_push_from_isr(
    microflow_supervisor_t *s, microflow_event_queue_t *q,
    microflow_supervisor_event_t event) {
    if (microflow_event_queue_push_from_isr(q, event)) {
        return true;
    }
    if (s != NULL) {
        s->fault = MICROFLOW_SUPERVISOR_FAULT_EVENT_QUEUE_OVERFLOW;
        ++s->fault_count;
        s->pending_events |= MICROFLOW_SUPERVISOR_EVENT_FAULT;
    }
    return false;
}

/* Remove the oldest complete event for the sole main-context consumer. */
bool microflow_event_queue_pop(microflow_event_queue_t      *q,
                               microflow_supervisor_event_t *out) {
    if ((q == NULL) || (out == NULL) || (q->read_index == q->write_index)) {
        return false;
    }
    *out          = q->storage[q->read_index];
    q->read_index = event_next(q->read_index);
    return true;
}

/* Decide whether a deadline arrived using the documented half-range time model.
 */
static bool time_arrived(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

/* Validate required platform capabilities and construct clean supervisor state.
 */
bool microflow_supervisor_initialize(microflow_supervisor_t                *s,
                                     const microflow_supervisor_platform_t *p,
                                     uint32_t                               period) {
    /* Half-range periods are required by the wrap-safe signed comparison. */
    if ((s == NULL) || (p == NULL) || (p->now == NULL) ||
        (p->wait_until == NULL) ||
        ((p->critical_enter == NULL) != (p->critical_exit == NULL)) ||
        (period == 0U) || (period > (uint32_t)INT32_MAX)) {
        return false;
    }
    s->platform               = *p;
    s->service_count          = 0U;
    s->pending_events         = 0U;
    s->fault                  = MICROFLOW_SUPERVISOR_FAULT_NONE;
    s->fault_count            = 0U;
    s->heartbeat              = 0U;
    s->watchdog_service_count = 0U;
    s->heartbeat_period       = period;
    s->heartbeat_deadline     = p->now(p->context) + period;
    return true;
}

/* Register one bounded cooperative service in stable numeric-priority order. */
bool microflow_supervisor_add_service(
    microflow_supervisor_t *s, uint8_t priority, uint32_t mask, uint32_t budget,
    microflow_supervisor_service_callback_t callback, void *context) {
    if ((s == NULL) || (callback == NULL) || (mask == 0U) ||
        (s->service_count >= MICROFLOW_SUPERVISOR_MAX_SERVICES)) {
        return false;
    }
    size_t pos = s->service_count;
    /* Insert rather than sort later so every dispatch is predictably ordered. */
    while ((pos != 0U) && (s->services[pos - 1U].priority > priority)) {
        s->services[pos] = s->services[pos - 1U];
        --pos;
    }
    s->services[pos].callback                 = callback;
    s->services[pos].context                  = context;
    s->services[pos].event_mask               = mask;
    s->services[pos].maximum_runtime_ticks    = budget;
    s->services[pos].observed_maximum_runtime = 0U;
    s->services[pos].dispatch_count           = 0U;
    s->services[pos].priority                 = priority;
    ++s->service_count;
    return true;
}

/* Coalesce interrupt events into bounded shared state for later dispatch. */
void microflow_supervisor_signal_from_isr(microflow_supervisor_t *s,
                                          uint32_t                events) {
    /* Coalescing flags bounds interrupt work and pending-state memory. */
    if (s != NULL) {
        s->pending_events |= events;
    }
}

/* Record a fault immediately but arrange platform reporting outside interrupt
 * code. */
void microflow_supervisor_report_fault(microflow_supervisor_t      *s,
                                       microflow_supervisor_fault_t fault) {
    if ((s == NULL) || (fault == MICROFLOW_SUPERVISOR_FAULT_NONE)) {
        return;
    }
    s->fault = fault;
    ++s->fault_count;
    s->pending_events |= MICROFLOW_SUPERVISOR_EVENT_FAULT;
}

/* Perform one heartbeat, event snapshot, ordered dispatch, watchdog, and idle
 * pass. */
bool microflow_supervisor_run_once(microflow_supervisor_t *s) {
    if (s == NULL) {
        return false;
    }
    uint32_t now = s->platform.now(s->platform.context);
    if (time_arrived(now, s->heartbeat_deadline)) {
        /* Advance from the prior cadence and skip missed beats without drift. */
        const uint32_t elapsed = now - s->heartbeat_deadline;
        s->heartbeat_deadline +=
            ((elapsed / s->heartbeat_period) + 1U) * s->heartbeat_period;
        ++s->heartbeat;
        s->pending_events |= MICROFLOW_SUPERVISOR_EVENT_TIMER;
    }
    /* Snapshot-and-clear must not lose an interrupt arriving between writes. */
    uint32_t critical_state = 0U;
    if (s->platform.critical_enter != NULL) {
        critical_state = s->platform.critical_enter(s->platform.context);
    }
    const uint32_t events = s->pending_events;
    s->pending_events     = 0U;
    if (s->platform.critical_exit != NULL) {
        s->platform.critical_exit(s->platform.context, critical_state);
    }
    /* Fault callbacks may be slow, so they execute only outside the ISR. */
    if (((events & MICROFLOW_SUPERVISOR_EVENT_FAULT) != 0U) &&
        (s->platform.fault_report != NULL)) {
        s->platform.fault_report(s->platform.context, s->fault);
    }
    for (size_t i = 0U; i < s->service_count; ++i) {
        microflow_supervisor_service_t *service = &s->services[i];
        /* Services see only subscribed bits, allowing one event snapshot fanout. */
        const uint32_t ready = events & service->event_mask;
        if (ready == 0U) {
            continue;
        }
        const uint32_t start = s->platform.now(s->platform.context);
        service->callback(service->context, ready);
        /* Unsigned elapsed time remains correct when the tick counter wraps. */
        const uint32_t runtime = s->platform.now(s->platform.context) - start;
        if (runtime > service->observed_maximum_runtime) {
            service->observed_maximum_runtime = runtime;
        }
        ++service->dispatch_count;
        if ((service->maximum_runtime_ticks != 0U) &&
            (runtime > service->maximum_runtime_ticks)) {
            microflow_supervisor_report_fault(
                s, MICROFLOW_SUPERVISOR_FAULT_SERVICE_OVERRUN);
        }
    }
    /* The watchdog is serviced on both busy and idle passes through the loop. */
    if (s->platform.watchdog_service != NULL) {
        s->platform.watchdog_service(s->platform.context);
        ++s->watchdog_service_count;
    }
    /* With no work, wait only until the next mandatory heartbeat deadline. */
    if (events == 0U) {
        s->platform.wait_until(s->platform.context, true, s->heartbeat_deadline);
    }
    return events != 0U;
}

/* Own the firmware main loop by repeatedly executing bounded supervisor passes.
 */
void microflow_supervisor_run(microflow_supervisor_t *s) {
    for (;;) {
        (void)microflow_supervisor_run_once(s);
    }
}
