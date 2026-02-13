#include "memory/memory.h"
#include <string.h>
#include <stdio.h>

void memory_init(Memory* mem)
{
    memset(mem, 0, sizeof(Memory));
}

void memory_reset(Memory* mem)
{
    memset(mem->sram, 0, SRAM_SIZE);
    /* Flash is NOT cleared on reset (it's non-volatile) */
}

Status memory_load_binary(Memory* mem, const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open file '%s'\n", path);
        return STATUS_ERROR;
    }

    size_t n = fread(mem->flash, 1, FLASH_SIZE, f);
    fclose(f);

    if (n == 0) {
        fprintf(stderr, "Error: file '%s' is empty or unreadable\n", path);
        return STATUS_ERROR;
    }

    printf("Loaded %zu bytes into flash\n", n);
    return STATUS_OK;
}

/* --- Internal helpers --- */

static uint32_t read_le(const uint8_t* base, uint32_t offset, uint8_t size)
{
    switch (size) {
    case 1: return base[offset];
    case 2: return (uint32_t)base[offset]
                 | ((uint32_t)base[offset + 1] << 8);
    case 4: return (uint32_t)base[offset]
                 | ((uint32_t)base[offset + 1] << 8)
                 | ((uint32_t)base[offset + 2] << 16)
                 | ((uint32_t)base[offset + 3] << 24);
    default: return 0;
    }
}

static void write_le(uint8_t* base, uint32_t offset, uint32_t value, uint8_t size)
{
    switch (size) {
    case 1:
        base[offset] = (uint8_t)value;
        break;
    case 2:
        base[offset]     = (uint8_t)(value);
        base[offset + 1] = (uint8_t)(value >> 8);
        break;
    case 4:
        base[offset]     = (uint8_t)(value);
        base[offset + 1] = (uint8_t)(value >> 8);
        base[offset + 2] = (uint8_t)(value >> 16);
        base[offset + 3] = (uint8_t)(value >> 24);
        break;
    }
}

/* --- Flash peripheral callbacks --- */

uint32_t memory_flash_read(void* ctx, uint32_t offset, uint8_t size)
{
    Memory* mem = (Memory*)ctx;
    if (offset + size > FLASH_SIZE) return 0;
    return read_le(mem->flash, offset, size);
}

Status memory_flash_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size)
{
    (void)ctx; (void)offset; (void)value; (void)size;
    /* Flash is read-only during normal execution */
    fprintf(stderr, "Warning: attempted write to flash at offset 0x%08X\n", offset);
    return STATUS_ERROR;
}

/* --- SRAM peripheral callbacks --- */

uint32_t memory_sram_read(void* ctx, uint32_t offset, uint8_t size)
{
    Memory* mem = (Memory*)ctx;
    if (offset + size > SRAM_SIZE) return 0;
    return read_le(mem->sram, offset, size);
}

Status memory_sram_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size)
{
    Memory* mem = (Memory*)ctx;
    if (offset + size > SRAM_SIZE) return STATUS_INVALID_ADDRESS;
    write_le(mem->sram, offset, value, size);
    return STATUS_OK;
}
