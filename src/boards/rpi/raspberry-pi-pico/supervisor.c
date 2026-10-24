#include "supervisor.h"
#include <limits.h>

static uint16_t byte_next(uint16_t i) { return (uint16_t)((i + 1U) % MICROFLOW_SUPERVISOR_BYTE_CAPACITY); }
void microflow_byte_ring_initialize(microflow_byte_ring_t *r) {
    if (r != NULL) { r->read_index = 0U; r->write_index = 0U; r->high_water = 0U; r->rejected = 0U; }
}
size_t microflow_byte_ring_count(const microflow_byte_ring_t *r) {
    return r == NULL ? 0U : (r->write_index + MICROFLOW_SUPERVISOR_BYTE_CAPACITY - r->read_index) % MICROFLOW_SUPERVISOR_BYTE_CAPACITY;
}
bool microflow_byte_ring_push_from_isr(microflow_byte_ring_t *r, uint8_t value) {
    if (r == NULL) return false;
    const uint16_t next = byte_next(r->write_index);
    if (next == r->read_index) { ++r->rejected; return false; }
    r->storage[r->write_index] = value; r->write_index = next;
    const size_t count = microflow_byte_ring_count(r);
    if (count > r->high_water) r->high_water = (uint16_t)count;
    return true;
}
bool microflow_supervisor_byte_ring_push_from_isr(
    microflow_supervisor_t *s, microflow_byte_ring_t *r, uint8_t value) {
    if (microflow_byte_ring_push_from_isr(r, value)) return true;
    if (s != NULL) { s->fault = MICROFLOW_SUPERVISOR_FAULT_BYTE_QUEUE_OVERFLOW;
        ++s->fault_count; s->pending_events |= MICROFLOW_SUPERVISOR_EVENT_FAULT; }
    return false;
}
bool microflow_byte_ring_pop(microflow_byte_ring_t *r, uint8_t *out) {
    if ((r == NULL) || (out == NULL) || (r->read_index == r->write_index)) return false;
    *out = r->storage[r->read_index]; r->read_index = byte_next(r->read_index); return true;
}
static uint8_t event_next(uint8_t i) { return (uint8_t)((i + 1U) % MICROFLOW_SUPERVISOR_EVENT_CAPACITY); }
void microflow_event_queue_initialize(microflow_event_queue_t *q) {
    if (q != NULL) { q->read_index = 0U; q->write_index = 0U; q->high_water = 0U; q->rejected = 0U; }
}
bool microflow_event_queue_push_from_isr(microflow_event_queue_t *q, microflow_supervisor_event_t event) {
    if (q == NULL) return false;
    const uint8_t next = event_next(q->write_index);
    if (next == q->read_index) { ++q->rejected; return false; }
    q->storage[q->write_index] = event; q->write_index = next;
    const uint8_t count = (uint8_t)((q->write_index + MICROFLOW_SUPERVISOR_EVENT_CAPACITY - q->read_index) % MICROFLOW_SUPERVISOR_EVENT_CAPACITY);
    if (count > q->high_water) q->high_water = count;
    return true;
}
bool microflow_supervisor_event_queue_push_from_isr(microflow_supervisor_t *s,
    microflow_event_queue_t *q, microflow_supervisor_event_t event) {
    if (microflow_event_queue_push_from_isr(q, event)) return true;
    if (s != NULL) { s->fault = MICROFLOW_SUPERVISOR_FAULT_EVENT_QUEUE_OVERFLOW;
        ++s->fault_count; s->pending_events |= MICROFLOW_SUPERVISOR_EVENT_FAULT; }
    return false;
}
bool microflow_event_queue_pop(microflow_event_queue_t *q, microflow_supervisor_event_t *out) {
    if ((q == NULL) || (out == NULL) || (q->read_index == q->write_index)) return false;
    *out = q->storage[q->read_index]; q->read_index = event_next(q->read_index); return true;
}
static bool time_arrived(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
bool microflow_supervisor_initialize(microflow_supervisor_t *s,
    const microflow_supervisor_platform_t *p, uint32_t period) {
    if ((s == NULL) || (p == NULL) || (p->now == NULL) || (p->wait_until == NULL) ||
        ((p->critical_enter == NULL) != (p->critical_exit == NULL)) ||
        (period == 0U) || (period > (uint32_t)INT32_MAX)) return false;
    s->platform = *p; s->service_count = 0U; s->pending_events = 0U;
    s->fault = MICROFLOW_SUPERVISOR_FAULT_NONE; s->fault_count = 0U;
    s->heartbeat = 0U; s->watchdog_service_count = 0U; s->heartbeat_period = period;
    s->heartbeat_deadline = p->now(p->context) + period; return true;
}
bool microflow_supervisor_add_service(microflow_supervisor_t *s, uint8_t priority,
    uint32_t mask, uint32_t budget, microflow_supervisor_service_callback_t callback, void *context) {
    if ((s == NULL) || (callback == NULL) || (mask == 0U) ||
        (s->service_count >= MICROFLOW_SUPERVISOR_MAX_SERVICES)) return false;
    size_t pos = s->service_count;
    while ((pos != 0U) && (s->services[pos - 1U].priority > priority)) {
        s->services[pos] = s->services[pos - 1U]; --pos;
    }
    s->services[pos].callback = callback; s->services[pos].context = context;
    s->services[pos].event_mask = mask; s->services[pos].maximum_runtime_ticks = budget;
    s->services[pos].observed_maximum_runtime = 0U; s->services[pos].dispatch_count = 0U;
    s->services[pos].priority = priority;
    ++s->service_count; return true;
}
void microflow_supervisor_signal_from_isr(microflow_supervisor_t *s, uint32_t events) {
    if (s != NULL) s->pending_events |= events;
}
void microflow_supervisor_report_fault(microflow_supervisor_t *s, microflow_supervisor_fault_t fault) {
    if ((s == NULL) || (fault == MICROFLOW_SUPERVISOR_FAULT_NONE)) return;
    s->fault = fault; ++s->fault_count; s->pending_events |= MICROFLOW_SUPERVISOR_EVENT_FAULT;
}
bool microflow_supervisor_run_once(microflow_supervisor_t *s) {
    if (s == NULL) return false;
    uint32_t now = s->platform.now(s->platform.context);
    if (time_arrived(now, s->heartbeat_deadline)) {
        const uint32_t elapsed = now - s->heartbeat_deadline;
        s->heartbeat_deadline += ((elapsed / s->heartbeat_period) + 1U) * s->heartbeat_period;
        ++s->heartbeat; s->pending_events |= MICROFLOW_SUPERVISOR_EVENT_TIMER;
    }
    uint32_t critical_state = 0U;
    if (s->platform.critical_enter != NULL)
        critical_state = s->platform.critical_enter(s->platform.context);
    const uint32_t events = s->pending_events; s->pending_events = 0U;
    if (s->platform.critical_exit != NULL)
        s->platform.critical_exit(s->platform.context, critical_state);
    if (((events & MICROFLOW_SUPERVISOR_EVENT_FAULT) != 0U) &&
        (s->platform.fault_report != NULL))
        s->platform.fault_report(s->platform.context, s->fault);
    for (size_t i = 0U; i < s->service_count; ++i) {
        microflow_supervisor_service_t *service = &s->services[i];
        const uint32_t ready = events & service->event_mask;
        if (ready == 0U) continue;
        const uint32_t start = s->platform.now(s->platform.context);
        service->callback(service->context, ready);
        const uint32_t runtime = s->platform.now(s->platform.context) - start;
        if (runtime > service->observed_maximum_runtime) service->observed_maximum_runtime = runtime;
        ++service->dispatch_count;
        if ((service->maximum_runtime_ticks != 0U) && (runtime > service->maximum_runtime_ticks))
            microflow_supervisor_report_fault(s, MICROFLOW_SUPERVISOR_FAULT_SERVICE_OVERRUN);
    }
    if (s->platform.watchdog_service != NULL) { s->platform.watchdog_service(s->platform.context); ++s->watchdog_service_count; }
    if (events == 0U) s->platform.wait_until(s->platform.context, true, s->heartbeat_deadline);
    return events != 0U;
}
void microflow_supervisor_run(microflow_supervisor_t *s) { for (;;) (void)microflow_supervisor_run_once(s); }
