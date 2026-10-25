/*
 * Cooperative board supervisor and bounded ISR-to-main-context queues.
 * All storage is caller-owned or embedded, so operation never allocates.
 */
#ifndef MICROFLOW_RASPBERRY_PI_PICO_SUPERVISOR_H
#define MICROFLOW_RASPBERRY_PI_PICO_SUPERVISOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bounds registered work and therefore each dispatch scan's execution cost. */
#define MICROFLOW_SUPERVISOR_MAX_SERVICES 8U
/* Storage slots for UART/console bytes; usable ring capacity is one less. */
#define MICROFLOW_SUPERVISOR_BYTE_CAPACITY 128U
/* Storage slots for structured ISR events; usable capacity is one less. */
#define MICROFLOW_SUPERVISOR_EVENT_CAPACITY 16U

/*
 * Both queues are static SPSC rings. The producer is one peripheral ISR and
 * the consumer is its supervisor service. Producer and consumer each write
 * only their own index, so no interrupt mask is needed for push/pop. A full
 * queue rejects the newest item, increments rejected, and preserves every
 * unread item. Use the supervisor_*_push wrapper when overflow must also set
 * the observable supervisor fault event. Effective capacity is N - 1.
 */

typedef enum {
    /* Periodic heartbeat deadline elapsed and timer-dependent work is ready. */
    MICROFLOW_SUPERVISOR_EVENT_TIMER = 1U << 0U,
    /* UART0 received bytes that the main-context RX service must drain. */
    MICROFLOW_SUPERVISOR_EVENT_UART0_RX = 1U << 1U,
    /* UART0 can accept more bytes from the bounded transmit queue. */
    MICROFLOW_SUPERVISOR_EVENT_UART0_TX = 1U << 2U,
    /* USB device state or CDC queues need servicing outside interrupt context. */
    MICROFLOW_SUPERVISOR_EVENT_USB = 1U << 3U,
    /* A deferred fault awaits reporting in safe main-loop context. */
    MICROFLOW_SUPERVISOR_EVENT_FAULT = 1U << 30U } microflow_supervisor_event_flag_t;
typedef enum {
    /* Explicit sentinel indicating that no fault has been recorded. */
    MICROFLOW_SUPERVISOR_FAULT_NONE = 0,
    /* An ISR produced a byte after its bounded SPSC queue became full. */
    MICROFLOW_SUPERVISOR_FAULT_BYTE_QUEUE_OVERFLOW,
    /* An ISR produced a structured event after its queue became full. */
    MICROFLOW_SUPERVISOR_FAULT_EVENT_QUEUE_OVERFLOW,
    /* A service exceeded its configured non-zero execution-time budget. */
    MICROFLOW_SUPERVISOR_FAULT_SERVICE_OVERRUN,
    /* Required platform or service configuration was invalid or incomplete. */
    MICROFLOW_SUPERVISOR_FAULT_CONFIGURATION } microflow_supervisor_fault_t;

/* Byte SPSC ring plus observable capacity and overflow diagnostics. */
typedef struct { uint8_t storage[MICROFLOW_SUPERVISOR_BYTE_CAPACITY];
    volatile uint16_t read_index, write_index; uint16_t high_water; uint32_t rejected;
} microflow_byte_ring_t;
/* Small generic event payload for interrupt-to-service communication. */
typedef struct { uint32_t kind, value; } microflow_supervisor_event_t;
/* Structured-event SPSC ring with the same reject-newest policy. */
typedef struct { microflow_supervisor_event_t storage[MICROFLOW_SUPERVISOR_EVENT_CAPACITY];
    volatile uint8_t read_index, write_index; uint8_t high_water; uint32_t rejected;
} microflow_event_queue_t;
/* Services receive their registered context and only matching event flags. */
typedef void (*microflow_supervisor_service_callback_t)(void *, uint32_t);
/* Registration plus bounded-runtime metrics for one cooperative service. */
typedef struct { microflow_supervisor_service_callback_t callback; void *context;
    uint32_t event_mask, maximum_runtime_ticks, observed_maximum_runtime,
        dispatch_count; uint8_t priority;
} microflow_supervisor_service_t;
/* Injected hardware primitives keep supervisor policy host-testable. */
typedef struct { uint32_t (*now)(void *); void (*wait_until)(void *, bool, uint32_t);
    void (*watchdog_service)(void *); void (*fault_report)(void *, microflow_supervisor_fault_t);
    uint32_t (*critical_enter)(void *); void (*critical_exit)(void *, uint32_t);
    void *context;
} microflow_supervisor_platform_t;
/* Complete statically sized state for one supervisor instance. */
typedef struct { microflow_supervisor_platform_t platform;
    microflow_supervisor_service_t services[MICROFLOW_SUPERVISOR_MAX_SERVICES];
    size_t service_count; volatile uint32_t pending_events;
    microflow_supervisor_fault_t fault; uint32_t fault_count, heartbeat,
        heartbeat_period, heartbeat_deadline, watchdog_service_count;
} microflow_supervisor_t;

/* Resets a byte ring and all of its diagnostics to an empty state. */
void microflow_byte_ring_initialize(microflow_byte_ring_t *ring);
/* Enqueues one byte from the sole ISR producer, rejecting newest on full. */
bool microflow_byte_ring_push_from_isr(microflow_byte_ring_t *ring, uint8_t value);
/* Enqueues a byte and raises an observable supervisor fault on overflow. */
bool microflow_supervisor_byte_ring_push_from_isr(microflow_supervisor_t *supervisor,
    microflow_byte_ring_t *ring, uint8_t value);
/* Removes the oldest byte in sole-consumer main context. */
bool microflow_byte_ring_pop(microflow_byte_ring_t *ring, uint8_t *value_out);
/* Returns current queued bytes for diagnostics and atomic reply checks. */
size_t microflow_byte_ring_count(const microflow_byte_ring_t *ring);
/* Resets a structured event queue and its diagnostics. */
void microflow_event_queue_initialize(microflow_event_queue_t *queue);
/* Enqueues one complete event from the queue's sole ISR producer. */
bool microflow_event_queue_push_from_isr(microflow_event_queue_t *queue,
    microflow_supervisor_event_t event);
/* Enqueues an event and raises a supervisor fault when bounded space is gone. */
bool microflow_supervisor_event_queue_push_from_isr(microflow_supervisor_t *supervisor,
    microflow_event_queue_t *queue, microflow_supervisor_event_t event);
/* Removes the oldest complete structured event in main context. */
bool microflow_event_queue_pop(microflow_event_queue_t *queue,
    microflow_supervisor_event_t *event_out);
/* Validates the platform and constructs clean supervisor runtime state. */
bool microflow_supervisor_initialize(microflow_supervisor_t *supervisor,
    const microflow_supervisor_platform_t *platform, uint32_t heartbeat_period);
/* Registers a service in ascending numeric priority order. */
bool microflow_supervisor_add_service(microflow_supervisor_t *supervisor,
    uint8_t priority, uint32_t event_mask, uint32_t maximum_runtime_ticks,
    microflow_supervisor_service_callback_t callback, void *context);
/* Atomically-visible OR of coalescible event flags from interrupt context. */
void microflow_supervisor_signal_from_isr(microflow_supervisor_t *supervisor,
    uint32_t events);
/* Records a fault and schedules its platform report for normal context. */
void microflow_supervisor_report_fault(microflow_supervisor_t *supervisor,
    microflow_supervisor_fault_t fault);
/* Performs one bounded dispatch/watchdog/idle iteration; reports work done. */
bool microflow_supervisor_run_once(microflow_supervisor_t *supervisor);
/* Runs supervisor iterations forever as the firmware's cooperative main loop. */
void microflow_supervisor_run(microflow_supervisor_t *supervisor);
#endif
