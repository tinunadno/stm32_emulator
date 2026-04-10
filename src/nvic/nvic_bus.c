#include "nvic/nvic_bus.h"
#include <string.h>

void nvic_bus_init(NvicBusState* nb, NVIC* nvic)
{
    nb->nvic = nvic;
}

Peripheral nvic_bus_as_peripheral(NvicBusState* nb)
{
    Peripheral p;
    memset(&p, 0, sizeof(p));
    p.ctx   = nb;
    p.read  = nvic_bus_read;
    p.write = nvic_bus_write;
    return p;
}

/* Build a 32-bit bitmask of enabled[] starting at IRQ 'base'. */
static uint32_t enabled_word(NVIC* nvic, int base)
{
    uint32_t v = 0;
    for (int i = 0; i < 32; i++) {
        int irq = base + i;
        if (irq < NVIC_NUM_IRQ && nvic->enabled[irq])
            v |= (1U << i);
    }
    return v;
}

/* Build a 32-bit bitmask of pending[] starting at IRQ 'base'. */
static uint32_t pending_word(NVIC* nvic, int base)
{
    uint32_t v = 0;
    for (int i = 0; i < 32; i++) {
        int irq = base + i;
        if (irq < NVIC_NUM_IRQ && nvic->pending[irq])
            v |= (1U << i);
    }
    return v;
}

/* Build a 32-bit bitmask of active[] starting at IRQ 'base'. */
static uint32_t active_word(NVIC* nvic, int base)
{
    uint32_t v = 0;
    for (int i = 0; i < 32; i++) {
        int irq = base + i;
        if (irq < NVIC_NUM_IRQ && nvic->active[irq])
            v |= (1U << i);
    }
    return v;
}

/* Read 4 priority bytes starting at IRQ 'base_irq'. */
static uint32_t priority_word(NVIC* nvic, uint32_t base_irq)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t irq = base_irq + (uint32_t)i;
        if (irq < NVIC_NUM_IRQ)
            v |= ((uint32_t)nvic->priority[irq]) << (i * 8);
    }
    return v;
}

uint32_t nvic_bus_read(void* ctx, uint32_t offset, uint8_t size)
{
    NvicBusState* nb = (NvicBusState*)ctx;
    NVIC* nvic = nb->nvic;
    (void)size;

    /* ISER0 / ISER1 */
    if (offset == 0x000) return enabled_word(nvic,  0);
    if (offset == 0x004) return enabled_word(nvic, 32);

    /* ICER0 / ICER1 — read returns same as ISER */
    if (offset == 0x080) return enabled_word(nvic,  0);
    if (offset == 0x084) return enabled_word(nvic, 32);

    /* ISPR0 / ISPR1 */
    if (offset == 0x100) return pending_word(nvic,  0);
    if (offset == 0x104) return pending_word(nvic, 32);

    /* ICPR0 / ICPR1 — read returns same as ISPR */
    if (offset == 0x180) return pending_word(nvic,  0);
    if (offset == 0x184) return pending_word(nvic, 32);

    /* IABR0 / IABR1 */
    if (offset == 0x200) return active_word(nvic,  0);
    if (offset == 0x204) return active_word(nvic, 32);

    /* IPR0..IPR10 — one 32-bit word per 4 IRQs */
    if (offset >= 0x300 && offset < 0x300 + 48U) {
        uint32_t word_idx = (offset - 0x300) / 4;
        return priority_word(nvic, word_idx * 4);
    }

    return 0;
}

Status nvic_bus_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size)
{
    NvicBusState* nb = (NvicBusState*)ctx;
    NVIC* nvic = nb->nvic;
    (void)size;

    /* ISER: writing a 1-bit enables the corresponding IRQ */
    if (offset == 0x000 || offset == 0x004) {
        int base = (offset == 0x000) ? 0 : 32;
        for (int i = 0; i < 32; i++) {
            if (value & (1U << i)) {
                int irq = base + i;
                if (irq < NVIC_NUM_IRQ)
                    nvic_enable_irq(nvic, (uint32_t)irq);
            }
        }
        return STATUS_OK;
    }

    /* ICER: writing a 1-bit disables the corresponding IRQ */
    if (offset == 0x080 || offset == 0x084) {
        int base = (offset == 0x080) ? 0 : 32;
        for (int i = 0; i < 32; i++) {
            if (value & (1U << i)) {
                int irq = base + i;
                if (irq < NVIC_NUM_IRQ)
                    nvic_disable_irq(nvic, (uint32_t)irq);
            }
        }
        return STATUS_OK;
    }

    /* ISPR: writing a 1-bit sets the IRQ pending */
    if (offset == 0x100 || offset == 0x104) {
        int base = (offset == 0x100) ? 0 : 32;
        for (int i = 0; i < 32; i++) {
            if (value & (1U << i)) {
                int irq = base + i;
                if (irq < NVIC_NUM_IRQ)
                    nvic_set_pending(nvic, (uint32_t)irq);
            }
        }
        return STATUS_OK;
    }

    /* ICPR: writing a 1-bit clears the IRQ pending */
    if (offset == 0x180 || offset == 0x184) {
        int base = (offset == 0x180) ? 0 : 32;
        for (int i = 0; i < 32; i++) {
            if (value & (1U << i)) {
                int irq = base + i;
                if (irq < NVIC_NUM_IRQ)
                    nvic_clear_pending(nvic, (uint32_t)irq);
            }
        }
        return STATUS_OK;
    }

    /* IABR: read-only, ignore writes */
    if (offset == 0x200 || offset == 0x204)
        return STATUS_OK;

    /* IPR: 4 priority bytes per word, byte order: IRQ(4n) at bits [7:0] */
    if (offset >= 0x300 && offset < 0x300 + 48U) {
        uint32_t word_idx = (offset - 0x300) / 4;
        uint32_t base_irq = word_idx * 4;
        for (int i = 0; i < 4; i++) {
            uint32_t irq = base_irq + (uint32_t)i;
            if (irq < NVIC_NUM_IRQ)
                nvic_set_priority(nvic, irq, (uint8_t)((value >> (i * 8)) & 0xFF));
        }
        return STATUS_OK;
    }

    return STATUS_OK;
}
