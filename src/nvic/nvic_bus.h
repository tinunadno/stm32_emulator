#ifndef STM32_NVIC_BUS_H
#define STM32_NVIC_BUS_H

#include <stdint.h>
#include "nvic/nvic.h"
#include "common/status.h"
#include "peripherals/peripheral.h"

/*
 * NVIC register-space bus peripheral (Cortex-M3).
 * Base: 0xE000E100, size: 0x400.
 *
 * Offsets from base (0xE000E100):
 *   0x000  ISER0  — Interrupt Set-Enable  (IRQ  0-31), write 1 to enable
 *   0x004  ISER1  — Interrupt Set-Enable  (IRQ 32-42)
 *   0x080  ICER0  — Interrupt Clr-Enable  (IRQ  0-31), write 1 to disable
 *   0x084  ICER1  — Interrupt Clr-Enable  (IRQ 32-42)
 *   0x100  ISPR0  — Set-Pending           (IRQ  0-31)
 *   0x104  ISPR1
 *   0x180  ICPR0  — Clear-Pending         (IRQ  0-31)
 *   0x184  ICPR1
 *   0x200  IABR0  — Active Bit (read-only)(IRQ  0-31)
 *   0x204  IABR1
 *   0x300  IPR0   — Priority: IRQ  0- 3 (1 byte each, bits [7:0])
 *   0x304  IPR1   — Priority: IRQ  4- 7
 *   ...
 *   0x32C  IPR11  — Priority: IRQ 44-47 (only up to IRQ42)
 *
 * TIM2 = IRQ28: enable via ISER0 bit 28, priority in IPR7 byte 0.
 */

#define NVIC_BUS_BASE  0xE000E100U
#define NVIC_BUS_SIZE  0x400U

typedef struct {
    NVIC* nvic;
} NvicBusState;

void       nvic_bus_init(NvicBusState* nb, NVIC* nvic);
Peripheral nvic_bus_as_peripheral(NvicBusState* nb);

uint32_t nvic_bus_read (void* ctx, uint32_t offset, uint8_t size);
Status   nvic_bus_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size);

#endif /* STM32_NVIC_BUS_H */
