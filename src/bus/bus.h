#ifndef STM32_BUS_H
#define STM32_BUS_H

#include <stdint.h>
#include "common/status.h"

/**
 * Bus region entry: maps an address range to a peripheral device.
 * Uses function pointers so any module can be a bus target.
 */
typedef struct {
    uint32_t  base;
    uint32_t  size;
    void*     ctx;
    uint32_t  (*read)(void* ctx, uint32_t offset, uint8_t size);
    Status    (*write)(void* ctx, uint32_t offset, uint32_t value, uint8_t size);
} BusRegion;

#define BUS_MAX_REGIONS 16

/**
 * System bus: routes memory accesses to the appropriate peripheral or memory.
 * Adding a new device to the address space requires only a register_region() call.
 */
typedef struct Bus {
    BusRegion regions[BUS_MAX_REGIONS];
    int       num_regions;
} Bus;

void     bus_init(Bus* bus);

/**
 * Register a new address region on the bus.
 * Regions must not overlap. Returns STATUS_OK on success.
 */
Status   bus_register_region(Bus* bus, uint32_t base, uint32_t size,
                             void* ctx,
                             uint32_t (*read_fn)(void*, uint32_t, uint8_t),
                             Status (*write_fn)(void*, uint32_t, uint32_t, uint8_t));

/** Read 1/2/4 bytes from the given address. Returns 0 on invalid address. */
uint32_t bus_read(Bus* bus, uint32_t addr, uint8_t size);

/** Write 1/2/4 bytes to the given address. */
Status   bus_write(Bus* bus, uint32_t addr, uint32_t value, uint8_t size);

#endif /* STM32_BUS_H */
