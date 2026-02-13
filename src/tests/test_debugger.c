#include "tests/test.h"
#include "debugger/debugger.h"

/* --- Test: add breakpoint and check hit --- */
static void test_debugger_add_check(void)
{
    Debugger dbg;
    debugger_init(&dbg);

    ASSERT_EQ(debugger_add_breakpoint(&dbg, 0x08000080), 0);
    ASSERT_EQ(dbg.num_breakpoints, 1);

    /* Check hit */
    ASSERT(debugger_check(&dbg, 0x08000080));
    /* Check miss */
    ASSERT(!debugger_check(&dbg, 0x08000082));
}

/* --- Test: remove breakpoint --- */
static void test_debugger_remove(void)
{
    Debugger dbg;
    debugger_init(&dbg);

    debugger_add_breakpoint(&dbg, 0x08000080);
    debugger_add_breakpoint(&dbg, 0x08000084);
    ASSERT_EQ(dbg.num_breakpoints, 2);

    /* Remove first */
    ASSERT_EQ(debugger_remove_breakpoint(&dbg, 0x08000080), 0);
    ASSERT_EQ(dbg.num_breakpoints, 1);
    ASSERT(!debugger_check(&dbg, 0x08000080));
    ASSERT(debugger_check(&dbg, 0x08000084));

    /* Remove non-existent → -1 */
    ASSERT_EQ(debugger_remove_breakpoint(&dbg, 0xDEADBEEF), (uint32_t)-1);
}

/* --- Test: duplicate breakpoint is not added twice --- */
static void test_debugger_duplicate(void)
{
    Debugger dbg;
    debugger_init(&dbg);

    debugger_add_breakpoint(&dbg, 0x08000100);
    debugger_add_breakpoint(&dbg, 0x08000100);
    ASSERT_EQ(dbg.num_breakpoints, 1);
}

/* --- Test: multiple breakpoints --- */
static void test_debugger_multiple(void)
{
    Debugger dbg;
    debugger_init(&dbg);

    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(debugger_add_breakpoint(&dbg, 0x08000000 + i * 4), 0);
    }
    ASSERT_EQ(dbg.num_breakpoints, 10);

    for (int i = 0; i < 10; i++) {
        ASSERT(debugger_check(&dbg, 0x08000000 + i * 4));
    }
    ASSERT(!debugger_check(&dbg, 0x08001000));
}

void test_debugger_all(void)
{
    TEST_SUITE("Debugger");
    RUN_TEST(test_debugger_add_check);
    RUN_TEST(test_debugger_remove);
    RUN_TEST(test_debugger_duplicate);
    RUN_TEST(test_debugger_multiple);
}
