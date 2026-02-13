#include "bus/bus.h"
#include <stdio.h>

void bus_init(Bus* bus)
{
    bus->num_regions = 0;
}

Status bus_register_region(Bus* bus, uint32_t base, uint32_t size,
                           void* ctx,
                           uint32_t (*read_fn)(void*, uint32_t, uint8_t),
                           Status (*write_fn)(void*, uint32_t, uint32_t, uint8_t))
{
    if (bus->num_regions >= BUS_MAX_REGIONS) {
        fprintf(stderr, "Error: bus region limit reached\n");
        return STATUS_ERROR;
    }

    BusRegion* r = &bus->regions[bus->num_regions++];
    r->base  = base;
    r->size  = size;
    r->ctx   = ctx;
    r->read  = read_fn;
    r->write = write_fn;

    return STATUS_OK;
}

static BusRegion* find_region(Bus* bus, uint32_t addr)
{
    for (int i = 0; i < bus->num_regions; i++) {
        BusRegion* r = &bus->regions[i];
        if (addr >= r->base && addr < r->base + r->size) {
            return r;
        }
    }
    return NULL;
}

uint32_t bus_read(Bus* bus, uint32_t addr, uint8_t size)
{
    BusRegion* r = find_region(bus, addr);
    if (r && r->read) {
        return r->read(r->ctx, addr - r->base, size);
    }
    fprintf(stderr, "Bus fault: read from unmapped address 0x%08X\n", addr);
    return 0;
}

Status bus_write(Bus* bus, uint32_t addr, uint32_t value, uint8_t size)
{
    BusRegion* r = find_region(bus, addr);
    if (r && r->write) {
        return r->write(r->ctx, addr - r->base, value, size);
    }
    fprintf(stderr, "Bus fault: write to unmapped address 0x%08X\n", addr);
    return STATUS_INVALID_ADDRESS;
}
