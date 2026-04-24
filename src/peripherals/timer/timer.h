#ifndef STM32_TIMER_H
#define STM32_TIMER_H

#include <stdint.h>
#include "common/status.h"
#include "nvic/nvic.h"
#include "events/event_queue.h"
#include "peripherals/peripheral.h"

/* TIM2 register offsets (from base 0x40000000) */
#define TIM_CR1_OFFSET   0x00
#define TIM_DIER_OFFSET  0x0C
#define TIM_SR_OFFSET    0x10
#define TIM_CNT_OFFSET   0x24
#define TIM_PSC_OFFSET   0x28
#define TIM_ARR_OFFSET   0x2C

/* CR1 bits */
#define TIM_CR1_CEN      (1U << 0)   /* Counter enable */

/* SR bits */
#define TIM_SR_UIF       (1U << 0)   /* Update interrupt flag */

/* DIER bits */
#define TIM_DIER_UIE     (1U << 0)   /* Update interrupt enable */

/**
 * TIM2 general-purpose timer.
 *
 * Uses the event queue instead of per-step polling.
 * CNT is computed on read from (cycle - start_cycle).
 */
typedef struct {
    /* Hardware registers */
    uint32_t  cr1;
    uint32_t  dier;
    uint32_t  sr;
    uint32_t  psc;
    uint32_t  arr;

    /* Event-driven state */
    EventQueue* eq;             /* Shared event queue (owned by Simulator) */
    const uint64_t* sim_cycle;  /* Pointer to Simulator.cycle               */
    uint64_t  start_cycle;      /* Cycle when counter was last reset to 0   */
    uint32_t  generation;       /* Incremented on reschedule/disable to cancel stale events */

    /* Dependencies */
    NVIC*     nvic;
    uint32_t  irq;
} TimerState;

void timer_init(TimerState* tim, NVIC* nvic, uint32_t irq,
                EventQueue* eq, const uint64_t* sim_cycle);

/** Fill a Peripheral struct for bus/simulator registration. */
Peripheral timer_as_peripheral(TimerState* tim);

/* Peripheral-compatible callbacks (tick is NULL — event-driven now) */
uint32_t timer_read(void* ctx, uint32_t offset, uint8_t size);
Status   timer_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size);
void     timer_reset(void* ctx);

#endif /* STM32_TIMER_H */
