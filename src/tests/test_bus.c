#include "tests/test.h"
#include "bus/bus.h"
#include "memory/memory.h"

/* --- Test: bus routes to correct region --- */
static void test_bus_routing(void)
{
    Memory mem;
    Bus bus;
    memory_init(&mem);
    bus_init(&bus);

    bus_register_region(&bus, FLASH_BASE, FLASH_SIZE,
                        &mem, memory_flash_read, memory_flash_write);
    bus_register_region(&bus, SRAM_BASE, SRAM_SIZE,
                        &mem, memory_sram_read, memory_sram_write);

    /* Write to SRAM via bus */
    ASSERT_EQ(bus_write(&bus, SRAM_BASE, 0x12345678, 4), STATUS_OK);

    /* Read back from SRAM via bus */
    ASSERT_EQ(bus_read(&bus, SRAM_BASE, 4), 0x12345678);

    /* Write to flash via bus → should fail (read-only) */
    ASSERT_EQ(bus_write(&bus, FLASH_BASE, 0xABCD, 4), STATUS_ERROR);

    /* Verify SRAM offset addressing works */
    bus_write(&bus, SRAM_BASE + 0x100, 0xAABBCCDD, 4);
    ASSERT_EQ(bus_read(&bus, SRAM_BASE + 0x100, 4), 0xAABBCCDD);
}

/* --- Test: flash alias at 0x00000000 --- */
static void test_bus_flash_alias(void)
{
    Memory mem;
    Bus bus;
    memory_init(&mem);
    bus_init(&bus);

    /* Register flash at both addresses */
    bus_register_region(&bus, 0x00000000, FLASH_SIZE,
                        &mem, memory_flash_read, memory_flash_write);
    bus_register_region(&bus, FLASH_BASE, FLASH_SIZE,
                        &mem, memory_flash_read, memory_flash_write);

    /* Write to flash directly */
    flash_write32(&mem, 0, 0x20005000);
    flash_write32(&mem, 4, 0x08000041);

    /* Read via 0x00000000 alias */
    ASSERT_EQ(bus_read(&bus, 0x00000000, 4), 0x20005000);
    ASSERT_EQ(bus_read(&bus, 0x00000004, 4), 0x08000041);

    /* Read via 0x08000000 */
    ASSERT_EQ(bus_read(&bus, FLASH_BASE, 4), 0x20005000);
    ASSERT_EQ(bus_read(&bus, FLASH_BASE + 4, 4), 0x08000041);
}

/* --- Test: unmapped address returns 0 / error --- */
static void test_bus_unmapped(void)
{
    Bus bus;
    bus_init(&bus);

    /* Read from unmapped address → 0 */
    ASSERT_EQ(bus_read(&bus, 0xFFFF0000, 4), 0);

    /* Write to unmapped address → error */
    ASSERT_EQ(bus_write(&bus, 0xFFFF0000, 0x42, 4), STATUS_INVALID_ADDRESS);
}

void test_bus_all(void)
{
    TEST_SUITE("Bus");
    RUN_TEST(test_bus_routing);
    RUN_TEST(test_bus_flash_alias);
    RUN_TEST(test_bus_unmapped);
}
