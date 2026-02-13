#include "tests/test.h"
#include "nvic/nvic.h"

/* --- Test: set_pending and get_pending --- */
static void test_nvic_set_pending(void)
{
    NVIC nvic;
    nvic_init(&nvic);

    /* No pending IRQs initially */
    uint32_t irq;
    ASSERT(!nvic_get_pending_irq(&nvic, &irq));

    /* Enable and pend IRQ 5 */
    nvic_enable_irq(&nvic, 5);
    nvic_set_pending(&nvic, 5);

    ASSERT(nvic_get_pending_irq(&nvic, &irq));
    ASSERT_EQ(irq, 5);
}

/* --- Test: disabled IRQs are not returned --- */
static void test_nvic_enable_disable(void)
{
    NVIC nvic;
    nvic_init(&nvic);

    /* Pend IRQ 10 but don't enable it */
    nvic_set_pending(&nvic, 10);
    uint32_t irq;
    ASSERT(!nvic_get_pending_irq(&nvic, &irq));

    /* Enable it → should now be returned */
    nvic_enable_irq(&nvic, 10);
    ASSERT(nvic_get_pending_irq(&nvic, &irq));
    ASSERT_EQ(irq, 10);

    /* Disable → gone again */
    nvic_disable_irq(&nvic, 10);
    ASSERT(!nvic_get_pending_irq(&nvic, &irq));
}

/* --- Test: priority ordering (lower value = higher priority) --- */
static void test_nvic_priority(void)
{
    NVIC nvic;
    nvic_init(&nvic);

    /* Enable and pend two IRQs with different priorities */
    nvic_enable_irq(&nvic, 3);
    nvic_enable_irq(&nvic, 7);
    nvic_set_priority(&nvic, 3, 10);  /* lower priority */
    nvic_set_priority(&nvic, 7, 2);   /* higher priority */
    nvic_set_pending(&nvic, 3);
    nvic_set_pending(&nvic, 7);

    uint32_t irq;
    ASSERT(nvic_get_pending_irq(&nvic, &irq));
    ASSERT_EQ(irq, 7);  /* Higher priority (lower number) wins */
}

/* --- Test: acknowledge clears pending, complete restores priority --- */
static void test_nvic_acknowledge_complete(void)
{
    NVIC nvic;
    nvic_init(&nvic);

    nvic_enable_irq(&nvic, 5);
    nvic_set_priority(&nvic, 5, 3);
    nvic_set_pending(&nvic, 5);

    /* Acknowledge: should clear pending and set active */
    nvic_acknowledge(&nvic, 5);
    ASSERT(!nvic.pending[5]);
    ASSERT(nvic.active[5]);
    ASSERT_EQ(nvic.current_priority, 3);

    /* While IRQ 5 is active, a lower-priority IRQ should NOT preempt */
    nvic_enable_irq(&nvic, 10);
    nvic_set_priority(&nvic, 10, 5);  /* Lower priority than 5's priority=3 */
    nvic_set_pending(&nvic, 10);

    uint32_t irq;
    ASSERT(!nvic_get_pending_irq(&nvic, &irq));

    /* But a higher-priority IRQ CAN preempt */
    nvic_enable_irq(&nvic, 1);
    nvic_set_priority(&nvic, 1, 1);
    nvic_set_pending(&nvic, 1);
    ASSERT(nvic_get_pending_irq(&nvic, &irq));
    ASSERT_EQ(irq, 1);

    /* Complete IRQ 5: should restore priority */
    nvic_complete(&nvic, 5);
    ASSERT(!nvic.active[5]);
    ASSERT_EQ(nvic.current_priority, 0xFF);  /* No more active */

    /* Now IRQ 10 should be reachable */
    ASSERT(nvic_get_pending_irq(&nvic, &irq));
    /* IRQ 1 has higher priority than 10, so 1 is still returned first */
    ASSERT_EQ(irq, 1);
}

/* --- Test: clear_pending --- */
static void test_nvic_clear_pending(void)
{
    NVIC nvic;
    nvic_init(&nvic);

    nvic_enable_irq(&nvic, 20);
    nvic_set_pending(&nvic, 20);

    uint32_t irq;
    ASSERT(nvic_get_pending_irq(&nvic, &irq));

    nvic_clear_pending(&nvic, 20);
    ASSERT(!nvic_get_pending_irq(&nvic, &irq));
}

void test_nvic_all(void)
{
    TEST_SUITE("NVIC");
    RUN_TEST(test_nvic_set_pending);
    RUN_TEST(test_nvic_enable_disable);
    RUN_TEST(test_nvic_priority);
    RUN_TEST(test_nvic_acknowledge_complete);
    RUN_TEST(test_nvic_clear_pending);
}
