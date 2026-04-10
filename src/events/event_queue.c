#include "events/event_queue.h"
#include <stdio.h>
#include <string.h>

/* ---------- min-heap helpers (ordered by when) ---------- */

static inline void swap(Event* a, Event* b)
{
    Event tmp = *a;
    *a = *b;
    *b = tmp;
}

static void sift_up(EventQueue* q, int i)
{
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (q->heap[parent].when <= q->heap[i].when)
            break;
        swap(&q->heap[parent], &q->heap[i]);
        i = parent;
    }
}

static void sift_down(EventQueue* q, int i)
{
    for (;;) {
        int smallest = i;
        int left  = 2 * i + 1;
        int right = 2 * i + 2;

        if (left  < q->size && q->heap[left].when  < q->heap[smallest].when)
            smallest = left;
        if (right < q->size && q->heap[right].when < q->heap[smallest].when)
            smallest = right;

        if (smallest == i)
            break;

        swap(&q->heap[i], &q->heap[smallest]);
        i = smallest;
    }
}

/* ---------- public API ---------- */

void event_queue_init(EventQueue* q)
{
    memset(q, 0, sizeof(EventQueue));
}

uint32_t event_queue_schedule(EventQueue* q, uint64_t when,
                              EventCallback fire, void* ctx)
{
    if (q->size >= EVENT_QUEUE_CAPACITY) {
        fprintf(stderr, "event_queue: capacity exceeded, dropping event\n");
        return 0;
    }

    uint32_t id = ++q->next_id;
    if (id == 0) id = ++q->next_id;  /* Skip 0 — reserved as "no id" */

    Event e = { .when = when, .fire = fire, .ctx = ctx, .id = id };
    q->heap[q->size++] = e;
    sift_up(q, q->size - 1);
    return id;
}

void event_queue_dispatch(EventQueue* q, uint64_t now)
{
    while (q->size > 0 && q->heap[0].when <= now) {
        /* Pop the earliest event */
        Event e = q->heap[0];
        q->heap[0] = q->heap[--q->size];
        if (q->size > 0)
            sift_down(q, 0);

        if (e.fire)
            e.fire(e.ctx);
    }
}

uint64_t event_queue_peek(const EventQueue* q)
{
    if (q->size == 0)
        return UINT64_MAX;
    return q->heap[0].when;
}
