#ifndef STM32_MEMORY_H
#define STM32_MEMORY_H

#include <stdint.h>
#include "common/status.h"

#define FLASH_BASE  0x08000000U
#define FLASH_SIZE  (64U * 1024U)   /* 64 KB */
#define SRAM_BASE   0x20000000U
#define SRAM_SIZE   (20U * 1024U)   /* 20 KB */

/**
 * Memory module holding Flash and SRAM storage.
 * Provides Peripheral-compatible read/write functions for bus registration.
 */
typedef struct {
    uint8_t flash[FLASH_SIZE];
    uint8_t sram[SRAM_SIZE];
} Memory;

void     memory_init(Memory* mem);
void     memory_reset(Memory* mem);

/** Load a raw binary file into flash at offset 0. */
Status   memory_load_binary(Memory* mem, const char* path);

/* --- Peripheral-compatible callbacks for Bus registration --- */

/** Flash read (offset relative to flash base). */
uint32_t memory_flash_read(void* ctx, uint32_t offset, uint8_t size);
/** Flash write (offset relative to flash base). */
Status   memory_flash_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size);

/** SRAM read (offset relative to SRAM base). */
uint32_t memory_sram_read(void* ctx, uint32_t offset, uint8_t size);
/** SRAM write (offset relative to SRAM base). */
Status   memory_sram_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size);

#endif /* STM32_MEMORY_H */
