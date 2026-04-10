#ifndef STM32_NVIC_H
#define STM32_NVIC_H

#include <stdint.h>

#define NVIC_NUM_IRQ 43

/**
 * Nested Vectored Interrupt Controller.
 * Manages 43 external IRQ lines plus core exceptions (SysTick, PendSV).
 * Priority: lower value = higher priority.
 */
typedef struct {
    int      pending[NVIC_NUM_IRQ];
    int      active[NVIC_NUM_IRQ];
    int      enabled[NVIC_NUM_IRQ];
    uint8_t  priority[NVIC_NUM_IRQ];
    uint8_t  current_priority;  /* Priority of currently executing exception (0xFF = none) */

    /* Core exceptions (not external IRQs) */
    int      systick_pending;
    int      systick_enabled;   /* SYST_CSR.TICKINT */
    uint8_t  systick_priority;
} NVIC;

void nvic_init(NVIC* nvic);
void nvic_reset(NVIC* nvic);

/** Mark IRQ as pending. */
void nvic_set_pending(NVIC* nvic, uint32_t irq);

/** Clear pending flag for IRQ. */
void nvic_clear_pending(NVIC* nvic, uint32_t irq);

/** Enable an IRQ line. */
void nvic_enable_irq(NVIC* nvic, uint32_t irq);

/** Disable an IRQ line. */
void nvic_disable_irq(NVIC* nvic, uint32_t irq);

/** Set priority for an IRQ (lower = higher priority). */
void nvic_set_priority(NVIC* nvic, uint32_t irq, uint8_t prio);

/**
 * Get the highest-priority pending & enabled IRQ that can preempt.
 * Returns 1 if an IRQ was found (written to *irq_out), 0 otherwise.
 */
int nvic_get_pending_irq(NVIC* nvic, uint32_t* irq_out);

/** Acknowledge an IRQ: mark active, clear pending. */
void nvic_acknowledge(NVIC* nvic, uint32_t irq);

/** Complete an IRQ: mark not active, restore priority. */
void nvic_complete(NVIC* nvic, uint32_t irq);

/* --- SysTick core exception --- */
void nvic_set_systick_pending(NVIC* nvic);
void nvic_clear_systick_pending(NVIC* nvic);
void nvic_enable_systick(NVIC* nvic, int enable);
void nvic_set_systick_priority(NVIC* nvic, uint8_t prio);

/**
 * Check if SysTick can fire (pending + enabled + priority high enough).
 * Returns 1 if SysTick should preempt current execution.
 */
int  nvic_get_systick_pending(NVIC* nvic);

#endif /* STM32_NVIC_H */
