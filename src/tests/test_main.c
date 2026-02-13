#include "tests/test.h"
#include <stdio.h>

/* Global counters used by test macros */
int test_pass_count = 0;
int test_fail_count = 0;

int main(void)
{
    printf("========================================\n");
    printf("  STM32 Simulator — Test Suite\n");
    printf("========================================\n");

    test_nvic_all();
    test_memory_all();
    test_bus_all();
    test_timer_all();
    test_uart_all();
    test_core_all();
    test_debugger_all();
    test_integration_all();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n",
           test_pass_count, test_fail_count);
    printf("========================================\n");

    return test_fail_count > 0 ? 1 : 0;
}
