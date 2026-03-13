#ifndef STM32_CORE_H
#define STM32_CORE_H

#include <stdint.h>
#include "common/status.h"

/* Forward declaration: Bus is defined in bus/bus.h */
struct Bus;

#define REG_SP 13
#define REG_LR 14
#define REG_PC 15

/**
 * ARM Cortex-M3 processor state.
 */
typedef struct {
    uint32_t r[16];       /* R0-R15 (R13=SP, R14=LR, R15=PC) */
    uint32_t xpsr;        /* Program Status Register (N, Z, C, V flags) */
    int      thumb_mode;  /* Always 1 for Cortex-M3 */
    int      interruptible;
    uint32_t current_irq; /* Currently active IRQ (0 = none, N+1 = IRQ N) */
    uint64_t cycles;
} CoreState;

/* xPSR flag bit positions */
#define XPSR_N (1U << 31)
#define XPSR_Z (1U << 30)
#define XPSR_C (1U << 29)
#define XPSR_V (1U << 28)

/**
 * ARM Cortex-M3 core.
 * Executes Thumb instructions fetched via the Bus.
 * Checks interrupts from the NVIC after each step.
 */
typedef struct Core {
    CoreState    state;
    struct Bus*  bus;
    void*        nvic;  /* Actually NVIC*, using void* to avoid circular include */
} Core;

void             core_init(Core* core, struct Bus* bus, void* nvic);
void             core_reset(Core* core);
Status           core_step(Core* core);
const CoreState* core_get_state(const Core* core);

#endif /* STM32_CORE_H */
