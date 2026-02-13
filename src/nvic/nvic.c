#include "nvic/nvic.h"
#include <string.h>

void nvic_init(NVIC* nvic)
{
    nvic_reset(nvic);
}

void nvic_reset(NVIC* nvic)
{
    memset(nvic->pending,  0, sizeof(nvic->pending));
    memset(nvic->active,   0, sizeof(nvic->active));
    memset(nvic->enabled,  0, sizeof(nvic->enabled));
    memset(nvic->priority, 0, sizeof(nvic->priority));
    nvic->current_priority = 0xFF; /* No active interrupt */
}

void nvic_set_pending(NVIC* nvic, uint32_t irq)
{
    if (irq < NVIC_NUM_IRQ)
        nvic->pending[irq] = 1;
}

void nvic_clear_pending(NVIC* nvic, uint32_t irq)
{
    if (irq < NVIC_NUM_IRQ)
        nvic->pending[irq] = 0;
}

void nvic_enable_irq(NVIC* nvic, uint32_t irq)
{
    if (irq < NVIC_NUM_IRQ)
        nvic->enabled[irq] = 1;
}

void nvic_disable_irq(NVIC* nvic, uint32_t irq)
{
    if (irq < NVIC_NUM_IRQ)
        nvic->enabled[irq] = 0;
}

void nvic_set_priority(NVIC* nvic, uint32_t irq, uint8_t prio)
{
    if (irq < NVIC_NUM_IRQ)
        nvic->priority[irq] = prio;
}

int nvic_get_pending_irq(NVIC* nvic, uint32_t* irq_out)
{
    uint8_t best_prio = nvic->current_priority;
    int     found     = 0;
    uint32_t best_irq = 0;

    for (uint32_t i = 0; i < NVIC_NUM_IRQ; i++) {
        if (nvic->pending[i] && nvic->enabled[i]) {
            if (nvic->priority[i] < best_prio) {
                best_prio = nvic->priority[i];
                best_irq  = i;
                found     = 1;
            }
        }
    }

    if (found) {
        *irq_out = best_irq;
    }
    return found;
}

void nvic_acknowledge(NVIC* nvic, uint32_t irq)
{
    if (irq < NVIC_NUM_IRQ) {
        nvic->pending[irq] = 0;
        nvic->active[irq]  = 1;
        nvic->current_priority = nvic->priority[irq];
    }
}

void nvic_complete(NVIC* nvic, uint32_t irq)
{
    if (irq < NVIC_NUM_IRQ) {
        nvic->active[irq] = 0;
    }

    /* Recalculate current_priority from remaining active IRQs */
    nvic->current_priority = 0xFF;
    for (uint32_t i = 0; i < NVIC_NUM_IRQ; i++) {
        if (nvic->active[i] && nvic->priority[i] < nvic->current_priority) {
            nvic->current_priority = nvic->priority[i];
        }
    }
}
