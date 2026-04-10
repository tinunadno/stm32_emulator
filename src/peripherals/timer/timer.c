#include "peripherals/timer/timer.h"
#include <string.h>
#include <stdio.h>

/* Forward declaration for the event callback */
static void timer_overflow_event(void* ctx);

/* ---------- helpers ---------- */

/* Cycles per one CNT increment = psc + 1 */
static inline uint64_t cycles_per_tick(const TimerState* tim)
{
    return (uint64_t)tim->psc + 1;
}

/*
 * Schedule the next overflow event.
 * Called whenever the timer is (re)started or reloaded.
 * The generation field acts as a cancel token: the event callback checks
 * whether its captured generation still matches tim->generation before acting.
 */
static void timer_schedule_next(TimerState* tim)
{
    if (!(tim->cr1 & TIM_CR1_CEN))
        return;
    if (tim->arr == 0)
        return;

    uint64_t cycles_to_overflow = cycles_per_tick(tim) * (uint64_t)tim->arr;
    uint64_t fire_at = *tim->sim_cycle + cycles_to_overflow;

    tim->generation++;
    event_queue_schedule(tim->eq, fire_at, timer_overflow_event, tim);
}

/* Compute CNT value at the current cycle (read-time calculation). */
static uint32_t timer_cnt_now(const TimerState* tim)
{
    if (!(tim->cr1 & TIM_CR1_CEN))
        return 0;
    if (tim->arr == 0)
        return 0;

    uint64_t elapsed = *tim->sim_cycle - tim->start_cycle;
    uint64_t cnt = (elapsed / cycles_per_tick(tim)) % tim->arr;
    return (uint32_t)cnt;
}

/* ---------- event callback ---------- */

/*
 * Called by the event queue when the timer overflows.
 * Uses tim->generation to discard stale events (e.g. after disable/reprogram).
 */
static void timer_overflow_event(void* ctx)
{
    TimerState* tim = (TimerState*)ctx;

    /* Stale event — timer was reprogrammed or disabled since this was scheduled */
    if (!(tim->cr1 & TIM_CR1_CEN))
        return;

    fprintf(stderr, "[TIM2] overflow event fired at cycle %llu, dier=0x%X irq=%u\n",
            (unsigned long long)*tim->sim_cycle, tim->dier, tim->irq);

    /* Set update interrupt flag */
    tim->sr |= TIM_SR_UIF;

    /* Reset counter reference point */
    tim->start_cycle = *tim->sim_cycle;

    /* Fire IRQ if enabled */
    if (tim->dier & TIM_DIER_UIE)
        nvic_set_pending(tim->nvic, tim->irq);

    /* Reschedule next overflow */
    timer_schedule_next(tim);
}

/* ---------- public API ---------- */

void timer_init(TimerState* tim, NVIC* nvic, uint32_t irq,
                EventQueue* eq, const uint64_t* sim_cycle)
{
    memset(tim, 0, sizeof(TimerState));
    tim->nvic      = nvic;
    tim->irq       = irq;
    tim->eq        = eq;
    tim->sim_cycle = sim_cycle;
    tim->arr       = 0xFFFFFFFF;
}

Peripheral timer_as_peripheral(TimerState* tim)
{
    Peripheral p;
    p.ctx   = tim;
    p.read  = timer_read;
    p.write = timer_write;
    p.tick  = NULL;   /* No polling — event-driven */
    p.reset = timer_reset;
    return p;
}

void timer_reset(void* ctx)
{
    TimerState* tim = (TimerState*)ctx;
    NVIC*           nvic      = tim->nvic;
    uint32_t        irq       = tim->irq;
    EventQueue*     eq        = tim->eq;
    const uint64_t* sim_cycle = tim->sim_cycle;

    memset(tim, 0, sizeof(TimerState));
    tim->nvic      = nvic;
    tim->irq       = irq;
    tim->eq        = eq;
    tim->sim_cycle = sim_cycle;
    tim->arr       = 0xFFFFFFFF;
}

uint32_t timer_read(void* ctx, uint32_t offset, uint8_t size)
{
    TimerState* tim = (TimerState*)ctx;
    (void)size;

    switch (offset) {
    case TIM_CR1_OFFSET:  return tim->cr1;
    case TIM_DIER_OFFSET: return tim->dier;
    case TIM_SR_OFFSET:   return tim->sr;
    case TIM_CNT_OFFSET:  return timer_cnt_now(tim);  /* Computed, not stored */
    case TIM_PSC_OFFSET:  return tim->psc;
    case TIM_ARR_OFFSET:  return tim->arr;
    default:
        fprintf(stderr, "Timer: read from unknown offset 0x%02X\n", offset);
        return 0;
    }
}

Status timer_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size)
{
    TimerState* tim = (TimerState*)ctx;
    (void)size;

    switch (offset) {
    case TIM_CR1_OFFSET: {
        uint32_t was_enabled = tim->cr1 & TIM_CR1_CEN;
        uint32_t now_enabled = value   & TIM_CR1_CEN;
        tim->cr1 = value;

        if (now_enabled && !was_enabled) {
            /* Timer just enabled: start counting from now */
            tim->start_cycle = *tim->sim_cycle;
            fprintf(stderr, "[TIM2] CEN=1 at cycle %llu, arr=%u psc=%u dier=0x%X\n",
                    (unsigned long long)*tim->sim_cycle, tim->arr, tim->psc, tim->dier);
            timer_schedule_next(tim);
        } else if (!now_enabled && was_enabled) {
            /* Timer disabled: bump generation to invalidate pending event */
            tim->generation++;
        }
        break;
    }
    case TIM_DIER_OFFSET: tim->dier = value; break;
    case TIM_SR_OFFSET:   tim->sr  &= value; break;  /* Write-0-to-clear */
    case TIM_CNT_OFFSET:
        /* CNT write resets the counter reference point */
        tim->start_cycle = *tim->sim_cycle - (uint64_t)value * cycles_per_tick(tim);
        break;
    case TIM_PSC_OFFSET:
        tim->psc = value;
        /* Reschedule if running */
        if (tim->cr1 & TIM_CR1_CEN) {
            tim->start_cycle = *tim->sim_cycle;
            timer_schedule_next(tim);
        }
        break;
    case TIM_ARR_OFFSET:
        tim->arr = value;
        if (tim->cr1 & TIM_CR1_CEN) {
            tim->start_cycle = *tim->sim_cycle;
            timer_schedule_next(tim);
        }
        break;
    default:
        fprintf(stderr, "Timer: write to unknown offset 0x%02X\n", offset);
        return STATUS_ERROR;
    }
    return STATUS_OK;
}
