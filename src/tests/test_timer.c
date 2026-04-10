#include "tests/test.h"
#include "peripherals/timer/timer.h"
#include "events/event_queue.h"
#include "nvic/nvic.h"

/* Advance cycle by n steps and dispatch events */
static void advance(EventQueue* eq, uint64_t* cycle, uint64_t n)
{
    *cycle += n;
    event_queue_dispatch(eq, *cycle);
}

/* Helper: enable timer via timer_write so scheduling is triggered */
static void enable_timer(TimerState* tim)
{
    timer_write(tim, TIM_CR1_OFFSET, TIM_CR1_CEN, 4);
}

/* --- Test: basic counting with CEN --- */
static void test_timer_basic_count(void)
{
    NVIC nvic;
    EventQueue eq;
    uint64_t cycle = 0;
    TimerState tim;

    nvic_init(&nvic);
    event_queue_init(&eq);
    timer_init(&tim, &nvic, 28, &eq, &cycle);

    /* Timer disabled → CNT stays 0 */
    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 0);

    /* Enable: arr=100, psc=0 */
    tim.arr = 100;
    tim.psc = 0;
    enable_timer(&tim);

    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 1);

    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 2);

    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 3);
}

/* --- Test: overflow resets CNT and sets UIF --- */
static void test_timer_overflow(void)
{
    NVIC nvic;
    EventQueue eq;
    uint64_t cycle = 0;
    TimerState tim;

    nvic_init(&nvic);
    event_queue_init(&eq);
    timer_init(&tim, &nvic, 28, &eq, &cycle);

    tim.arr = 3;
    tim.psc = 0;
    enable_timer(&tim);

    /* cycle=1: CNT=1, no overflow */
    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 1);
    ASSERT_EQ(tim.sr & TIM_SR_UIF, 0);

    /* cycle=2: CNT=2, no overflow */
    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 2);
    ASSERT_EQ(tim.sr & TIM_SR_UIF, 0);

    /* cycle=3: overflow fires → CNT=0, UIF set */
    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 0);
    ASSERT(tim.sr & TIM_SR_UIF);
}

/* --- Test: prescaler divides tick rate --- */
static void test_timer_prescaler(void)
{
    NVIC nvic;
    EventQueue eq;
    uint64_t cycle = 0;
    TimerState tim;

    nvic_init(&nvic);
    event_queue_init(&eq);
    timer_init(&tim, &nvic, 28, &eq, &cycle);

    tim.arr = 100;
    tim.psc = 2;   /* cycles_per_tick = 3 */
    enable_timer(&tim);

    /* cycles 1,2: CNT still 0 */
    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 0);
    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 0);

    /* cycle 3: CNT=1 */
    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 1);

    /* cycles 4,5: still 1 */
    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 1);
    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 1);

    /* cycle 6: CNT=2 */
    advance(&eq, &cycle, 1);
    ASSERT_EQ(timer_read(&tim, TIM_CNT_OFFSET, 4), 2);
}

/* --- Test: overflow generates NVIC pending when DIER.UIE is set --- */
static void test_timer_irq_generation(void)
{
    NVIC nvic;
    EventQueue eq;
    uint64_t cycle = 0;
    TimerState tim;

    nvic_init(&nvic);
    event_queue_init(&eq);
    timer_init(&tim, &nvic, 28, &eq, &cycle);

    tim.arr  = 2;
    tim.psc  = 0;
    tim.dier = TIM_DIER_UIE;
    nvic_enable_irq(&nvic, 28);
    enable_timer(&tim);

    /* cycle=1: no overflow yet */
    advance(&eq, &cycle, 1);
    ASSERT(!nvic.pending[28]);

    /* cycle=2: overflow → IRQ pending */
    advance(&eq, &cycle, 1);
    ASSERT(nvic.pending[28]);
    ASSERT(tim.sr & TIM_SR_UIF);
}

/* --- Test: no IRQ if DIER.UIE is not set --- */
static void test_timer_overflow_no_irq(void)
{
    NVIC nvic;
    EventQueue eq;
    uint64_t cycle = 0;
    TimerState tim;

    nvic_init(&nvic);
    event_queue_init(&eq);
    timer_init(&tim, &nvic, 28, &eq, &cycle);

    tim.arr  = 1;
    tim.psc  = 0;
    tim.dier = 0;
    nvic_enable_irq(&nvic, 28);
    enable_timer(&tim);

    /* cycle=1: overflow, UIF set but no NVIC */
    advance(&eq, &cycle, 1);
    ASSERT(tim.sr & TIM_SR_UIF);
    ASSERT(!nvic.pending[28]);
}

void test_timer_all(void)
{
    TEST_SUITE("Timer (TIM2)");
    RUN_TEST(test_timer_basic_count);
    RUN_TEST(test_timer_overflow);
    RUN_TEST(test_timer_prescaler);
    RUN_TEST(test_timer_irq_generation);
    RUN_TEST(test_timer_overflow_no_irq);
}
