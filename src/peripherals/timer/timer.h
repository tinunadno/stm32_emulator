#ifndef STM32_TIMER_H
#define STM32_TIMER_H

#include <stdint.h>
#include "common/status.h"
#include "nvic/nvic.h"
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
 */
typedef struct {
    uint32_t  cr1;
    uint32_t  dier;
    uint32_t  sr;
    uint32_t  cnt;
    uint32_t  psc;
    uint32_t  arr;
    uint32_t  prescaler_counter;  /* Internal prescaler tick counter */
    NVIC*     nvic;
    uint32_t  irq;                /* IRQ number for this timer */
} TimerState;

void timer_init(TimerState* tim, NVIC* nvic, uint32_t irq);

/** Fill a Peripheral struct for bus/simulator registration. */
Peripheral timer_as_peripheral(TimerState* tim);

/* Peripheral-compatible callbacks */
uint32_t timer_read(void* ctx, uint32_t offset, uint8_t size);
Status   timer_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size);
void     timer_tick(void* ctx);
void     timer_reset(void* ctx);

#endif /* STM32_TIMER_H */
