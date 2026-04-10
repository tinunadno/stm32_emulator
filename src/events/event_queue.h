#ifndef STM32_EVENT_QUEUE_H
#define STM32_EVENT_QUEUE_H

#include <stdint.h>

#define EVENT_QUEUE_CAPACITY 256

typedef void (*EventCallback)(void* ctx);

typedef struct {
    uint64_t      when;   /* Absolute cycle number when this fires */
    EventCallback fire;
    void*         ctx;
    uint32_t      id;     /* Generation id — used to cancel stale events */
} Event;

typedef struct {
    Event    heap[EVENT_QUEUE_CAPACITY];
    int      size;
    uint32_t next_id;  /* Monotonically increasing, for cancellation */
} EventQueue;

void     event_queue_init(EventQueue* q);

/**
 * Schedule an event at absolute cycle `when`.
 * Returns the assigned event id (pass to event_queue_cancel to invalidate).
 */
uint32_t event_queue_schedule(EventQueue* q, uint64_t when,
                              EventCallback fire, void* ctx);

/**
 * Dispatch all events with when <= now.
 * Calls each event's fire() callback in chronological order.
 */
void     event_queue_dispatch(EventQueue* q, uint64_t now);

/**
 * Return the cycle of the earliest pending event.
 * Returns UINT64_MAX if the queue is empty.
 */
uint64_t event_queue_peek(const EventQueue* q);

#endif /* STM32_EVENT_QUEUE_H */
