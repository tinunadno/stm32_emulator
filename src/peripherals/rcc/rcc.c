#include "peripherals/rcc/rcc.h"
#include <string.h>
#include <stdio.h>

/* ---- reset values ---- */

static void rcc_load_reset_values(RccState* rcc)
{
    memset(rcc->regs, 0, sizeof(rcc->regs));

    /* CR: HSI on and ready, HSITRIM = 16 (bits 7:3) */
    rcc->regs[RCC_REG_CR] = RCC_CR_HSION | RCC_CR_HSIRDY | (16U << 3);

    /* CFGR: HSI as system clock, SWS reflects SW=0 */
    rcc->regs[RCC_REG_CFGR] = 0x00000000U;

    /* AHBENR: SRAM and FLITF interface clocks enabled (bits 2 and 4) */
    rcc->regs[RCC_REG_AHBENR] = 0x00000014U;

    /* CSR: mark power-on and pin reset as reason for last reset */
    rcc->regs[RCC_REG_CSR] = RCC_CSR_RESET_FLAGS;
}

/*
 * After writing CR, update the read-only "ready" flags to follow their
 * corresponding enable bits immediately (stub simplification).
 */
static void rcc_update_cr_ready(RccState* rcc)
{
    uint32_t cr = rcc->regs[RCC_REG_CR];

    if (cr & RCC_CR_HSION)  cr |= RCC_CR_HSIRDY;  else cr &= ~RCC_CR_HSIRDY;
    if (cr & RCC_CR_HSEON)  cr |= RCC_CR_HSERDY;  else cr &= ~RCC_CR_HSERDY;
    if (cr & RCC_CR_PLLON)  cr |= RCC_CR_PLLRDY;  else cr &= ~RCC_CR_PLLRDY;

    rcc->regs[RCC_REG_CR] = cr;
}

/*
 * After writing CFGR, set SWS = SW so firmware doesn't wait for the
 * "clock switch complete" status to change.
 */
static void rcc_update_cfgr_sws(RccState* rcc)
{
    uint32_t cfgr = rcc->regs[RCC_REG_CFGR];
    uint32_t sw   = cfgr & RCC_CFGR_SW_MASK;

    cfgr &= ~RCC_CFGR_SWS_MASK;
    cfgr |= (sw << RCC_CFGR_SWS_SHIFT);
    rcc->regs[RCC_REG_CFGR] = cfgr;
}

/* ---- public API ---- */

void rcc_init(RccState* rcc)
{
    rcc_load_reset_values(rcc);
}

Peripheral rcc_as_peripheral(RccState* rcc)
{
    Peripheral p;
    p.ctx   = rcc;
    p.read  = rcc_read;
    p.write = rcc_write;
    p.tick  = NULL;
    p.reset = rcc_reset;
    return p;
}

void rcc_reset(void* ctx)
{
    rcc_load_reset_values((RccState*)ctx);
}

uint32_t rcc_read(void* ctx, uint32_t offset, uint8_t size)
{
    RccState* rcc = (RccState*)ctx;
    (void)size;

    if (offset % 4 != 0 || offset / 4 >= RCC_NUM_REGS) {
        fprintf(stderr, "RCC: read from unknown offset 0x%02X\n", offset);
        return 0;
    }
    return rcc->regs[offset / 4];
}

Status rcc_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size)
{
    RccState* rcc = (RccState*)ctx;
    (void)size;

    if (offset % 4 != 0 || offset / 4 >= RCC_NUM_REGS) {
        fprintf(stderr, "RCC: write to unknown offset 0x%02X\n", offset);
        return STATUS_ERROR;
    }

    uint32_t idx = offset / 4;

    switch (idx) {
    case RCC_REG_CR:
        /* Preserve read-only HSIRDY/HSERDY/PLLRDY — we recompute them */
        rcc->regs[idx] = value & ~(RCC_CR_HSIRDY | RCC_CR_HSERDY | RCC_CR_PLLRDY);
        rcc_update_cr_ready(rcc);
        break;

    case RCC_REG_CFGR:
        /* SWS bits are read-only — firmware writes SW, we set SWS */
        rcc->regs[idx] = value & ~RCC_CFGR_SWS_MASK;
        rcc_update_cfgr_sws(rcc);
        break;

    case RCC_REG_CSR:
        /* Bit 24 (RMVF) clears reset flags when written 1 */
        if (value & (1U << 24))
            rcc->regs[idx] &= ~0xFF000000U;
        else
            rcc->regs[idx] = value;
        break;

    default:
        rcc->regs[idx] = value;
        break;
    }

    return STATUS_OK;
}
