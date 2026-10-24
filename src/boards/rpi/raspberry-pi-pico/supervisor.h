#ifndef MICROFLOW_RASPBERRY_PI_PICO_SUPERVISOR_H
#define MICROFLOW_RASPBERRY_PI_PICO_SUPERVISOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MICROFLOW_SUPERVISOR_MAX_SERVICES 8U
#define MICROFLOW_SUPERVISOR_BYTE_CAPACITY 128U
#define MICROFLOW_SUPERVISOR_EVENT_CAPACITY 16U

/*
 * Both queues are static SPSC rings. The producer is one peripheral ISR and
 * the consumer is its supervisor service. Producer and consumer each write
 * only their own index, so no interrupt mask is needed for push/pop. A full
 * queue rejects the newest item, increments rejected, and preserves every
 * unread item. Use the supervisor_*_push wrapper when overflow must also set
 * the observable supervisor fault event. Effective capacity is N - 1.
 */

typedef enum { MICROFLOW_SUPERVISOR_EVENT_TIMER = 1U << 0U,
    MICROFLOW_SUPERVISOR_EVENT_UART0_RX = 1U << 1U,
    MICROFLOW_SUPERVISOR_EVENT_UART0_TX = 1U << 2U,
    MICROFLOW_SUPERVISOR_EVENT_USB = 1U << 3U,
    MICROFLOW_SUPERVISOR_EVENT_FAULT = 1U << 30U } microflow_supervisor_event_flag_t;
typedef enum { MICROFLOW_SUPERVISOR_FAULT_NONE = 0,
    MICROFLOW_SUPERVISOR_FAULT_BYTE_QUEUE_OVERFLOW,
    MICROFLOW_SUPERVISOR_FAULT_EVENT_QUEUE_OVERFLOW,
    MICROFLOW_SUPERVISOR_FAULT_SERVICE_OVERRUN,
    MICROFLOW_SUPERVISOR_FAULT_CONFIGURATION } microflow_supervisor_fault_t;

typedef struct { uint8_t storage[MICROFLOW_SUPERVISOR_BYTE_CAPACITY];
    volatile uint16_t read_index, write_index; uint16_t high_water; uint32_t rejected;
} microflow_byte_ring_t;
typedef struct { uint32_t kind, value; } microflow_supervisor_event_t;
typedef struct { microflow_supervisor_event_t storage[MICROFLOW_SUPERVISOR_EVENT_CAPACITY];
    volatile uint8_t read_index, write_index; uint8_t high_water; uint32_t rejected;
} microflow_event_queue_t;
typedef void (*microflow_supervisor_service_callback_t)(void *, uint32_t);
typedef struct { microflow_supervisor_service_callback_t callback; void *context;
    uint32_t event_mask, maximum_runtime_ticks, observed_maximum_runtime,
        dispatch_count; uint8_t priority;
} microflow_supervisor_service_t;
typedef struct { uint32_t (*now)(void *); void (*wait_until)(void *, bool, uint32_t);
    void (*watchdog_service)(void *); void (*fault_report)(void *, microflow_supervisor_fault_t);
    uint32_t (*critical_enter)(void *); void (*critical_exit)(void *, uint32_t);
    void *context;
} microflow_supervisor_platform_t;
typedef struct { microflow_supervisor_platform_t platform;
    microflow_supervisor_service_t services[MICROFLOW_SUPERVISOR_MAX_SERVICES];
    size_t service_count; volatile uint32_t pending_events;
    microflow_supervisor_fault_t fault; uint32_t fault_count, heartbeat,
        heartbeat_period, heartbeat_deadline, watchdog_service_count;
} microflow_supervisor_t;

void microflow_byte_ring_initialize(microflow_byte_ring_t *ring);
bool microflow_byte_ring_push_from_isr(microflow_byte_ring_t *ring, uint8_t value);
bool microflow_supervisor_byte_ring_push_from_isr(microflow_supervisor_t *supervisor,
    microflow_byte_ring_t *ring, uint8_t value);
bool microflow_byte_ring_pop(microflow_byte_ring_t *ring, uint8_t *value_out);
size_t microflow_byte_ring_count(const microflow_byte_ring_t *ring);
void microflow_event_queue_initialize(microflow_event_queue_t *queue);
bool microflow_event_queue_push_from_isr(microflow_event_queue_t *queue,
    microflow_supervisor_event_t event);
bool microflow_supervisor_event_queue_push_from_isr(microflow_supervisor_t *supervisor,
    microflow_event_queue_t *queue, microflow_supervisor_event_t event);
bool microflow_event_queue_pop(microflow_event_queue_t *queue,
    microflow_supervisor_event_t *event_out);
bool microflow_supervisor_initialize(microflow_supervisor_t *supervisor,
    const microflow_supervisor_platform_t *platform, uint32_t heartbeat_period);
bool microflow_supervisor_add_service(microflow_supervisor_t *supervisor,
    uint8_t priority, uint32_t event_mask, uint32_t maximum_runtime_ticks,
    microflow_supervisor_service_callback_t callback, void *context);
void microflow_supervisor_signal_from_isr(microflow_supervisor_t *supervisor,
    uint32_t events);
void microflow_supervisor_report_fault(microflow_supervisor_t *supervisor,
    microflow_supervisor_fault_t fault);
bool microflow_supervisor_run_once(microflow_supervisor_t *supervisor);
void microflow_supervisor_run(microflow_supervisor_t *supervisor);
#endif
