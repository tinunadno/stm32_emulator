#ifndef STM32_PERIPHERAL_H
#define STM32_PERIPHERAL_H

#include <stdint.h>
#include "common/status.h"

/**
 * Generic peripheral interface (vtable pattern in C).
 *
 * To add a new peripheral to the simulator:
 *   1. Define your peripheral state struct
 *   2. Implement read/write/tick/reset functions matching the signatures below
 *   3. Fill a Peripheral struct with your function pointers and context
 *   4. Register it with simulator_add_peripheral()
 *
 * The bus uses read/write for memory-mapped register access.
 * The simulator calls tick() once per step and reset() on system reset.
 * Set unused function pointers to NULL.
 */
typedef struct {
    void* ctx;  /* Opaque pointer to peripheral-specific state */

    /** Read a register at the given offset (relative to peripheral base). */
    uint32_t (*read)(void* ctx, uint32_t offset, uint8_t size);

    /** Write a value to a register at the given offset. */
    Status (*write)(void* ctx, uint32_t offset, uint32_t value, uint8_t size);

    /** Called once per simulator step. NULL if peripheral has no time behavior. */
    void (*tick)(void* ctx);

    /** Reset peripheral to initial state. NULL if not needed. */
    void (*reset)(void* ctx);
} Peripheral;

#endif /* STM32_PERIPHERAL_H */
