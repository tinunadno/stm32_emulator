#ifndef STM32_SYSTICK_H
#define STM32_SYSTICK_H

#include <stdint.h>
#include "common/status.h"
#include "nvic/nvic.h"
#include "events/event_queue.h"
#include "peripherals/peripheral.h"

/*
 * SysTick — ARM Cortex-M3 core timer.
 * Mapped at 0xE000E010 (System Control Space).
 * Exception number 15, vector at 0x3C.
 *
 * Registers (offsets from 0xE000E010):
 *   0x00  SYST_CSR   — Control and Status
 *   0x04  SYST_RVR   — Reload Value
 *   0x08  SYST_CVR   — Current Value (write clears to 0)
 *   0x0C  SYST_CALIB — Calibration (read-only)
 */

#define SYST_BASE       0xE000E010U
#define SYST_SIZE       0x10U

#define SYST_CSR_OFFSET    0x00
#define SYST_RVR_OFFSET    0x04
#define SYST_CVR_OFFSET    0x08
#define SYST_CALIB_OFFSET  0x0C

/* CSR bits */
#define SYST_CSR_ENABLE    (1U << 0)  /* Counter enable */
#define SYST_CSR_TICKINT   (1U << 1)  /* Exception enable */
#define SYST_CSR_CLKSOURCE (1U << 2)  /* 1 = processor clock */
#define SYST_CSR_COUNTFLAG (1U << 16) /* Set when counter reaches 0, cleared on read */

/* CALIB fixed value: 1ms at 8 MHz (8000-1 = 7999 = 0x1F3F) */
#define SYST_CALIB_VALUE   0x00001F3FU

typedef struct {
    /* Registers */
    uint32_t  csr;
    uint32_t  rvr;   /* Reload value (24-bit, 0 = disabled) */

    /* Event-driven state */
    EventQueue*     eq;
    const uint64_t* sim_cycle;
    uint64_t        start_cycle;  /* Cycle when current countdown started */
    uint32_t        generation;

    NVIC* nvic;
} SysTickState;

void systick_init(SysTickState* st, NVIC* nvic,
                  EventQueue* eq, const uint64_t* sim_cycle);

Peripheral systick_as_peripheral(SysTickState* st);

uint32_t systick_read(void* ctx, uint32_t offset, uint8_t size);
Status   systick_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size);
void     systick_reset(void* ctx);

#endif /* STM32_SYSTICK_H */
