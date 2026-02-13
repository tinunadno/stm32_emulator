#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdint.h>

/* ======================================================================
 * Minimal test framework
 * ====================================================================== */

extern int test_pass_count;
extern int test_fail_count;

/**
 * Assert condition is true. On failure, prints location and returns
 * from the current test function (skipping the rest of the test).
 */
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        test_fail_count++; \
        return; \
    } \
    test_pass_count++; \
} while(0)

/**
 * Assert two uint32 values are equal.
 */
#define ASSERT_EQ(actual, expected) do { \
    uint32_t _a = (uint32_t)(actual); \
    uint32_t _e = (uint32_t)(expected); \
    if (_a != _e) { \
        printf("    FAIL %s:%d: %s == 0x%X, expected %s == 0x%X\n", \
               __FILE__, __LINE__, #actual, _a, #expected, _e); \
        test_fail_count++; \
        return; \
    } \
    test_pass_count++; \
} while(0)

/**
 * Assert two uint64 values are equal.
 */
#define ASSERT_EQ64(actual, expected) do { \
    uint64_t _a = (uint64_t)(actual); \
    uint64_t _e = (uint64_t)(expected); \
    if (_a != _e) { \
        printf("    FAIL %s:%d: %s == %llu, expected %s == %llu\n", \
               __FILE__, __LINE__, #actual, \
               (unsigned long long)_a, #expected, (unsigned long long)_e); \
        test_fail_count++; \
        return; \
    } \
    test_pass_count++; \
} while(0)

/**
 * Run a single test function and print result.
 */
#define RUN_TEST(fn) do { \
    int _before = test_fail_count; \
    fn(); \
    if (test_fail_count == _before) \
        printf("  %-50s PASS\n", #fn); \
    else \
        printf("  %-50s FAIL\n", #fn); \
} while(0)

/**
 * Print suite header.
 */
#define TEST_SUITE(name) printf("\n=== %s ===\n", name)

/* ======================================================================
 * Helper: write little-endian values to flash memory (for test setup)
 * ====================================================================== */

#include "memory/memory.h"

static inline void flash_write16(Memory* mem, uint32_t offset, uint16_t value)
{
    mem->flash[offset]     = (uint8_t)(value);
    mem->flash[offset + 1] = (uint8_t)(value >> 8);
}

static inline void flash_write32(Memory* mem, uint32_t offset, uint32_t value)
{
    mem->flash[offset]     = (uint8_t)(value);
    mem->flash[offset + 1] = (uint8_t)(value >> 8);
    mem->flash[offset + 2] = (uint8_t)(value >> 16);
    mem->flash[offset + 3] = (uint8_t)(value >> 24);
}

/* ======================================================================
 * Test suite declarations
 * ====================================================================== */

void test_nvic_all(void);
void test_memory_all(void);
void test_bus_all(void);
void test_timer_all(void);
void test_uart_all(void);
void test_core_all(void);
void test_debugger_all(void);
void test_integration_all(void);

#endif /* TEST_H */
