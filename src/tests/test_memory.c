#include "tests/test.h"
#include "memory/memory.h"
#include <string.h>

/* --- Test: SRAM read/write across sizes (little-endian) --- */
static void test_memory_sram_rw_sizes(void)
{
    Memory mem;
    memory_init(&mem);

    /* Write a 32-bit word */
    ASSERT_EQ(memory_sram_write(&mem, 0, 0xDEADBEEF, 4), STATUS_OK);

    /* Read back as word */
    ASSERT_EQ(memory_sram_read(&mem, 0, 4), 0xDEADBEEF);

    /* Read back as bytes (little-endian) */
    ASSERT_EQ(memory_sram_read(&mem, 0, 1), 0xEF);
    ASSERT_EQ(memory_sram_read(&mem, 1, 1), 0xBE);
    ASSERT_EQ(memory_sram_read(&mem, 2, 1), 0xAD);
    ASSERT_EQ(memory_sram_read(&mem, 3, 1), 0xDE);

    /* Read as halfwords */
    ASSERT_EQ(memory_sram_read(&mem, 0, 2), 0xBEEF);
    ASSERT_EQ(memory_sram_read(&mem, 2, 2), 0xDEAD);

    /* Write a single byte */
    ASSERT_EQ(memory_sram_write(&mem, 0, 0x42, 1), STATUS_OK);
    ASSERT_EQ(memory_sram_read(&mem, 0, 1), 0x42);
    /* Rest of the word should be unchanged */
    ASSERT_EQ(memory_sram_read(&mem, 1, 1), 0xBE);
}

/* --- Test: flash is loaded and readable --- */
static void test_memory_flash_read(void)
{
    Memory mem;
    memory_init(&mem);

    /* Manually write to flash (simulating load_binary) */
    flash_write32(&mem, 0, 0x20005000);
    flash_write32(&mem, 4, 0x08000041);

    ASSERT_EQ(memory_flash_read(&mem, 0, 4), 0x20005000);
    ASSERT_EQ(memory_flash_read(&mem, 4, 4), 0x08000041);
}

/* --- Test: flash write returns error (read-only) --- */
static void test_memory_flash_readonly(void)
{
    Memory mem;
    memory_init(&mem);

    ASSERT_EQ(memory_flash_write(&mem, 0, 0x12345678, 4), STATUS_ERROR);
}

/* --- Test: SRAM boundary access --- */
static void test_memory_sram_boundary(void)
{
    Memory mem;
    memory_init(&mem);

    /* Write at last valid SRAM address (offset SRAM_SIZE - 4) */
    uint32_t last_offset = SRAM_SIZE - 4;
    ASSERT_EQ(memory_sram_write(&mem, last_offset, 0xCAFEBABE, 4), STATUS_OK);
    ASSERT_EQ(memory_sram_read(&mem, last_offset, 4), 0xCAFEBABE);

    /* Write past the end → should fail */
    ASSERT_EQ(memory_sram_write(&mem, SRAM_SIZE, 0x1234, 4), STATUS_INVALID_ADDRESS);
}

/* --- Test: memory_reset clears SRAM but not flash --- */
static void test_memory_reset(void)
{
    Memory mem;
    memory_init(&mem);

    flash_write32(&mem, 0, 0xAABBCCDD);
    memory_sram_write(&mem, 0, 0x11223344, 4);

    memory_reset(&mem);

    /* Flash should survive reset */
    ASSERT_EQ(memory_flash_read(&mem, 0, 4), 0xAABBCCDD);

    /* SRAM should be cleared */
    ASSERT_EQ(memory_sram_read(&mem, 0, 4), 0x00000000);
}

void test_memory_all(void)
{
    TEST_SUITE("Memory");
    RUN_TEST(test_memory_sram_rw_sizes);
    RUN_TEST(test_memory_flash_read);
    RUN_TEST(test_memory_flash_readonly);
    RUN_TEST(test_memory_sram_boundary);
    RUN_TEST(test_memory_reset);
}
