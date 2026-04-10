#ifndef STM32_RCC_H
#define STM32_RCC_H

#include <stdint.h>
#include "common/status.h"
#include "peripherals/peripheral.h"

/*
 * RCC — Reset and Clock Control (STM32F103).
 * Base: 0x40021000, size: 0x400.
 *
 * This is a stub: ready flags follow enable flags immediately so that
 * HAL_RCC_OscConfig / HAL_RCC_ClockConfig don't spin-wait forever.
 * The clock tree is not simulated — the emulator runs at fixed 1 cycle/step.
 *
 * Register map:
 *   0x00  CR        — Clock control
 *   0x04  CFGR      — Clock configuration
 *   0x08  CIR       — Clock interrupt
 *   0x0C  APB2RSTR  — APB2 peripheral reset
 *   0x10  APB1RSTR  — APB1 peripheral reset
 *   0x14  AHBENR    — AHB peripheral clock enable
 *   0x18  APB2ENR   — APB2 peripheral clock enable
 *   0x1C  APB1ENR   — APB1 peripheral clock enable
 *   0x20  BDCR      — Backup domain control
 *   0x24  CSR       — Control / status
 */

#define RCC_BASE  0x40021000U
#define RCC_SIZE  0x400U

/* CR offsets and bits */
#define RCC_CR_OFFSET      0x00
#define RCC_CR_HSION       (1U <<  0)
#define RCC_CR_HSIRDY      (1U <<  1)   /* Read-only in HW, stub tracks HSION */
#define RCC_CR_HSEON       (1U << 16)
#define RCC_CR_HSERDY      (1U << 17)   /* Stub: immediate */
#define RCC_CR_PLLON       (1U << 24)
#define RCC_CR_PLLRDY      (1U << 25)   /* Stub: immediate */

/* CFGR offsets and bits */
#define RCC_CFGR_OFFSET    0x04
#define RCC_CFGR_SW_MASK   (3U <<  0)   /* Clock source select */
#define RCC_CFGR_SWS_SHIFT 2            /* Clock source status (read-only) */
#define RCC_CFGR_SWS_MASK  (3U <<  2)

/* CSR reset-flag defaults (power-on + pin reset flags set on boot) */
#define RCC_CSR_OFFSET     0x24
#define RCC_CSR_RESET_FLAGS 0x0C000000U  /* PINRSTF | PORRSTF */

/* Register indices (used internally) */
#define RCC_REG_CR      0
#define RCC_REG_CFGR    1
#define RCC_REG_CIR     2
#define RCC_REG_APB2RSTR 3
#define RCC_REG_APB1RSTR 4
#define RCC_REG_AHBENR  5
#define RCC_REG_APB2ENR 6
#define RCC_REG_APB1ENR 7
#define RCC_REG_BDCR    8
#define RCC_REG_CSR     9
#define RCC_NUM_REGS    10

typedef struct {
    uint32_t regs[RCC_NUM_REGS];
} RccState;

void rcc_init(RccState* rcc);

Peripheral rcc_as_peripheral(RccState* rcc);

uint32_t rcc_read(void* ctx, uint32_t offset, uint8_t size);
Status   rcc_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size);
void     rcc_reset(void* ctx);

#endif /* STM32_RCC_H */
