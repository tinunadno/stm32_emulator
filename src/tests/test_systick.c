#include "tests/test.h"
#include "peripherals/systick/systick.h"
#include "events/event_queue.h"
#include "nvic/nvic.h"

static void advance(EventQueue* eq, uint64_t* cycle, uint64_t n)
{
    *cycle += n;
    event_queue_dispatch(eq, *cycle);
}

/* Helper: enable SysTick with given reload value (and optionally TICKINT) */
static void enable_systick(SysTickState* st, uint32_t rvr, int tickint)
{
    systick_write(st, SYST_RVR_OFFSET, rvr, 4);
    uint32_t csr = SYST_CSR_ENABLE | SYST_CSR_CLKSOURCE;
    if (tickint) csr |= SYST_CSR_TICKINT;
    systick_write(st, SYST_CSR_OFFSET, csr, 4);
}

/* --- Test: CVR counts down from RVR --- */
static void test_systick_countdown(void)
{
    NVIC nvic;
    EventQueue eq;
    uint64_t cycle = 0;
    SysTickState st;

    nvic_init(&nvic);
    event_queue_init(&eq);
    systick_init(&st, &nvic, &eq, &cycle);

    enable_systick(&st, 9, 0);  /* RVR=9 → period = 10 cycles */

    /* At cycle 0 (start), CVR = 9 */
    ASSERT_EQ(systick_read(&st, SYST_CVR_OFFSET, 4), 9);

    advance(&eq, &cycle, 3);
    ASSERT_EQ(systick_read(&st, SYST_CVR_OFFSET, 4), 6);  /* 9 - 3 */

    advance(&eq, &cycle, 6);
    ASSERT_EQ(systick_read(&st, SYST_CVR_OFFSET, 4), 0);  /* 9 - 9 */
}

/* --- Test: COUNTFLAG set on reload, cleared on CSR read --- */
static void test_systick_countflag(void)
{
    NVIC nvic;
    EventQueue eq;
    uint64_t cycle = 0;
    SysTickState st;

    nvic_init(&nvic);
    event_queue_init(&eq);
    systick_init(&st, &nvic, &eq, &cycle);

    enable_systick(&st, 4, 0);  /* Period = 5 */

    /* Before overflow: no flag */
    advance(&eq, &cycle, 4);
    ASSERT_EQ(systick_read(&st, SYST_CSR_OFFSET, 4) & SYST_CSR_COUNTFLAG, 0);

    /* After overflow: flag set */
    advance(&eq, &cycle, 1);
    ASSERT(systick_read(&st, SYST_CSR_OFFSET, 4) & SYST_CSR_COUNTFLAG);

    /* Second read: flag cleared */
    ASSERT_EQ(systick_read(&st, SYST_CSR_OFFSET, 4) & SYST_CSR_COUNTFLAG, 0);
}

/* --- Test: TICKINT raises SysTick exception pending --- */
static void test_systick_tickint(void)
{
    NVIC nvic;
    EventQueue eq;
    uint64_t cycle = 0;
    SysTickState st;

    nvic_init(&nvic);
    event_queue_init(&eq);
    systick_init(&st, &nvic, &eq, &cycle);

    enable_systick(&st, 2, 1);  /* Period = 3, TICKINT=1 */

    advance(&eq, &cycle, 2);
    ASSERT(!nvic.systick_pending);

    advance(&eq, &cycle, 1);
    ASSERT(nvic.systick_pending);  /* Overflow at cycle 3 */
}

/* --- Test: CVR write restarts countdown --- */
static void test_systick_cvr_write_resets(void)
{
    NVIC nvic;
    EventQueue eq;
    uint64_t cycle = 0;
    SysTickState st;

    nvic_init(&nvic);
    event_queue_init(&eq);
    systick_init(&st, &nvic, &eq, &cycle);

    enable_systick(&st, 9, 0);

    advance(&eq, &cycle, 5);  /* CVR = 4 */
    ASSERT_EQ(systick_read(&st, SYST_CVR_OFFSET, 4), 4);

    /* Writing CVR resets count */
    systick_write(&st, SYST_CVR_OFFSET, 0, 4);
    ASSERT_EQ(systick_read(&st, SYST_CVR_OFFSET, 4), 9);

    advance(&eq, &cycle, 3);
    ASSERT_EQ(systick_read(&st, SYST_CVR_OFFSET, 4), 6);
}

/* --- Test: disabled SysTick doesn't fire --- */
static void test_systick_disabled(void)
{
    NVIC nvic;
    EventQueue eq;
    uint64_t cycle = 0;
    SysTickState st;

    nvic_init(&nvic);
    event_queue_init(&eq);
    systick_init(&st, &nvic, &eq, &cycle);

    /* Don't enable — just set RVR */
    systick_write(&st, SYST_RVR_OFFSET, 5, 4);

    advance(&eq, &cycle, 100);
    ASSERT(!nvic.systick_pending);
    ASSERT_EQ(systick_read(&st, SYST_CVR_OFFSET, 4), 0);  /* Not counting */
}

void test_systick_all(void)
{
    TEST_SUITE("SysTick");
    RUN_TEST(test_systick_countdown);
    RUN_TEST(test_systick_countflag);
    RUN_TEST(test_systick_tickint);
    RUN_TEST(test_systick_cvr_write_resets);
    RUN_TEST(test_systick_disabled);
}
