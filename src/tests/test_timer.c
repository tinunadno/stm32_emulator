#include "tests/test.h"
#include "peripherals/timer/timer.h"
#include "nvic/nvic.h"

/* --- Test: basic counting with CEN --- */
static void test_timer_basic_count(void)
{
    NVIC nvic;
    TimerState tim;
    nvic_init(&nvic);
    timer_init(&tim, &nvic, 28);

    /* Timer disabled → tick should not increment CNT */
    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 0);

    /* Enable timer */
    tim.cr1 = TIM_CR1_CEN;
    tim.arr = 100;
    tim.psc = 0;

    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 1);

    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 2);

    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 3);
}

/* --- Test: overflow resets CNT and sets UIF --- */
static void test_timer_overflow(void)
{
    NVIC nvic;
    TimerState tim;
    nvic_init(&nvic);
    timer_init(&tim, &nvic, 28);

    tim.cr1 = TIM_CR1_CEN;
    tim.arr = 3;
    tim.psc = 0;

    /* Tick 1: CNT=1 */
    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 1);
    ASSERT_EQ(tim.sr & TIM_SR_UIF, 0);

    /* Tick 2: CNT=2 */
    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 2);
    ASSERT_EQ(tim.sr & TIM_SR_UIF, 0);

    /* Tick 3: CNT=3, overflow! CNT→0, UIF set */
    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 0);
    ASSERT(tim.sr & TIM_SR_UIF);
}

/* --- Test: prescaler divides tick rate --- */
static void test_timer_prescaler(void)
{
    NVIC nvic;
    TimerState tim;
    nvic_init(&nvic);
    timer_init(&tim, &nvic, 28);

    tim.cr1 = TIM_CR1_CEN;
    tim.arr = 100;
    tim.psc = 2;  /* Divide by 3: count every 3 ticks */

    /* Ticks 1,2: prescaler absorbs, CNT stays 0 */
    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 0);
    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 0);

    /* Tick 3: prescaler fires, CNT=1 */
    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 1);

    /* Ticks 4,5: prescaler absorbs again */
    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 1);
    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 1);

    /* Tick 6: CNT=2 */
    timer_tick(&tim);
    ASSERT_EQ(tim.cnt, 2);
}

/* --- Test: overflow generates NVIC pending when DIER.UIE is set --- */
static void test_timer_irq_generation(void)
{
    NVIC nvic;
    TimerState tim;
    nvic_init(&nvic);
    timer_init(&tim, &nvic, 28);

    tim.cr1  = TIM_CR1_CEN;
    tim.arr  = 2;
    tim.psc  = 0;
    tim.dier = TIM_DIER_UIE;  /* Enable update interrupt */

    nvic_enable_irq(&nvic, 28);

    /* Tick 1: CNT=1, no overflow */
    timer_tick(&tim);
    ASSERT(!nvic.pending[28]);

    /* Tick 2: CNT=2 >= ARR=2, overflow! */
    timer_tick(&tim);
    ASSERT(nvic.pending[28]);
    ASSERT(tim.sr & TIM_SR_UIF);
}

/* --- Test: no IRQ if DIER.UIE is not set --- */
static void test_timer_overflow_no_irq(void)
{
    NVIC nvic;
    TimerState tim;
    nvic_init(&nvic);
    timer_init(&tim, &nvic, 28);

    tim.cr1  = TIM_CR1_CEN;
    tim.arr  = 1;
    tim.psc  = 0;
    tim.dier = 0;  /* UIE not set */

    nvic_enable_irq(&nvic, 28);

    timer_tick(&tim);
    /* UIF should be set but NVIC should NOT be pending */
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
