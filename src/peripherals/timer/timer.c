#include "peripherals/timer/timer.h"
#include <string.h>
#include <stdio.h>

void timer_init(TimerState* tim, NVIC* nvic, uint32_t irq)
{
    memset(tim, 0, sizeof(TimerState));
    tim->nvic = nvic;
    tim->irq  = irq;
    tim->arr  = 0xFFFFFFFF;  /* Default: max auto-reload value */
}

Peripheral timer_as_peripheral(TimerState* tim)
{
    Peripheral p;
    p.ctx   = tim;
    p.read  = timer_read;
    p.write = timer_write;
    p.tick  = timer_tick;
    p.reset = timer_reset;
    return p;
}

void timer_reset(void* ctx)
{
    TimerState* tim = (TimerState*)ctx;
    NVIC* nvic = tim->nvic;
    uint32_t irq = tim->irq;
    memset(tim, 0, sizeof(TimerState));
    tim->nvic = nvic;
    tim->irq  = irq;
    tim->arr  = 0xFFFFFFFF;
}

uint32_t timer_read(void* ctx, uint32_t offset, uint8_t size)
{
    TimerState* tim = (TimerState*)ctx;
    (void)size;

    switch (offset) {
    case TIM_CR1_OFFSET:  return tim->cr1;
    case TIM_DIER_OFFSET: return tim->dier;
    case TIM_SR_OFFSET:   return tim->sr;
    case TIM_CNT_OFFSET:  return tim->cnt;
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
    case TIM_CR1_OFFSET:  tim->cr1  = value; break;
    case TIM_DIER_OFFSET: tim->dier = value; break;
    case TIM_SR_OFFSET:   tim->sr  &= value; break;  /* Write-0-to-clear */
    case TIM_CNT_OFFSET:  tim->cnt  = value; break;
    case TIM_PSC_OFFSET:  tim->psc  = value; break;
    case TIM_ARR_OFFSET:  tim->arr  = value; break;
    default:
        fprintf(stderr, "Timer: write to unknown offset 0x%02X\n", offset);
        return STATUS_ERROR;
    }
    return STATUS_OK;
}

void timer_tick(void* ctx)
{
    TimerState* tim = (TimerState*)ctx;

    /* Only count when enabled */
    if (!(tim->cr1 & TIM_CR1_CEN))
        return;

    /* Prescaler divides the input clock */
    tim->prescaler_counter++;
    if (tim->prescaler_counter <= tim->psc)
        return;

    tim->prescaler_counter = 0;

    /* Increment counter */
    tim->cnt++;

    /* Check for overflow (CNT >= ARR) */
    if (tim->cnt >= tim->arr && tim->arr > 0) {
        tim->cnt = 0;
        tim->sr |= TIM_SR_UIF;

        /* Generate interrupt if enabled */
        if (tim->dier & TIM_DIER_UIE) {
            nvic_set_pending(tim->nvic, tim->irq);
        }
    }
}
