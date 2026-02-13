#include "core/core.h"
#include "bus/bus.h"
#include "nvic/nvic.h"
#include <string.h>
#include <stdio.h>

/* ======================================================================
 * Constants
 * ====================================================================== */

#define SP  REG_SP
#define LR  REG_LR
#define PC  REG_PC

/* EXC_RETURN magic values */
#define EXC_RETURN_HANDLER   0xFFFFFFF1U
#define EXC_RETURN_THREAD_MSP 0xFFFFFFF9U
#define EXC_RETURN_THREAD_PSP 0xFFFFFFFDU

#define IS_EXC_RETURN(val) (((val) & 0xFFFFFFF0U) == 0xFFFFFFF0U)

/* Shorthand for register access */
#define R(n)    (core->state.r[(n)])
#define XPSR    (core->state.xpsr)
#define CYCLES  (core->state.cycles)

/* ======================================================================
 * Flag helpers
 * ====================================================================== */

static void update_nz(Core* core, uint32_t result)
{
    XPSR &= ~(XPSR_N | XPSR_Z);
    if (result == 0)          XPSR |= XPSR_Z;
    if (result & 0x80000000U) XPSR |= XPSR_N;
}

static void set_flag(Core* core, uint32_t flag, int cond)
{
    if (cond) XPSR |= flag;
    else      XPSR &= ~flag;
}

static void update_flags_add(Core* core, uint32_t a, uint32_t b, uint32_t result)
{
    update_nz(core, result);
    uint64_t sum = (uint64_t)a + (uint64_t)b;
    set_flag(core, XPSR_C, sum > 0xFFFFFFFFU);
    set_flag(core, XPSR_V, (~(a ^ b) & (a ^ result)) >> 31);
}

static void update_flags_sub(Core* core, uint32_t a, uint32_t b, uint32_t result)
{
    update_nz(core, result);
    set_flag(core, XPSR_C, a >= b);  /* No borrow = carry set */
    set_flag(core, XPSR_V, ((a ^ b) & (a ^ result)) >> 31);
}

static int condition_passed(Core* core, uint8_t cond)
{
    int n = (XPSR >> 31) & 1;
    int z = (XPSR >> 30) & 1;
    int c = (XPSR >> 29) & 1;
    int v = (XPSR >> 28) & 1;

    switch (cond) {
    case 0x0: return z;              /* EQ */
    case 0x1: return !z;             /* NE */
    case 0x2: return c;              /* CS/HS */
    case 0x3: return !c;             /* CC/LO */
    case 0x4: return n;              /* MI */
    case 0x5: return !n;             /* PL */
    case 0x6: return v;              /* VS */
    case 0x7: return !v;             /* VC */
    case 0x8: return c && !z;        /* HI */
    case 0x9: return !c || z;        /* LS */
    case 0xA: return n == v;         /* GE */
    case 0xB: return n != v;         /* LT */
    case 0xC: return !z && (n == v); /* GT */
    case 0xD: return z || (n != v);  /* LE */
    case 0xE: return 1;              /* AL */
    default:  return 0;
    }
}

/* ======================================================================
 * Sign-extend helpers
 * ====================================================================== */

static int32_t sign_extend(uint32_t value, int bits)
{
    uint32_t mask = 1U << (bits - 1);
    return (int32_t)((value ^ mask) - mask);
}

/* ======================================================================
 * Exception (interrupt) entry/exit
 * ====================================================================== */

static void enter_exception(Core* core, uint32_t irq)
{
    NVIC* nvic = (NVIC*)core->nvic;
    Bus*  bus  = core->bus;

    /* Push context: xPSR, PC, LR, R12, R3, R2, R1, R0 (descending) */
    R(SP) -= 32;
    uint32_t frame = R(SP);
    bus_write(bus, frame + 0,  R(0),  4);
    bus_write(bus, frame + 4,  R(1),  4);
    bus_write(bus, frame + 8,  R(2),  4);
    bus_write(bus, frame + 12, R(3),  4);
    bus_write(bus, frame + 16, R(12), 4);
    bus_write(bus, frame + 20, R(LR), 4);
    bus_write(bus, frame + 24, R(PC), 4);
    bus_write(bus, frame + 28, XPSR,  4);

    /* Set LR to EXC_RETURN for Thread mode / MSP */
    R(LR) = EXC_RETURN_THREAD_MSP;

    /* Read vector table entry: IRQ N handler is at vector 16+N */
    uint32_t vector_addr = (16 + irq) * 4;
    uint32_t handler     = bus_read(bus, vector_addr, 4);
    R(PC) = handler & ~1U;  /* Clear Thumb bit for PC */

    /* Mark IRQ as acknowledged */
    nvic_acknowledge(nvic, irq);
    core->state.current_irq = irq + 1;  /* +1 so 0 means "no IRQ" */
}

static void exit_exception(Core* core)
{
    NVIC* nvic = (NVIC*)core->nvic;
    Bus*  bus  = core->bus;

    /* Pop context from stack */
    uint32_t frame = R(SP);
    R(0)  = bus_read(bus, frame + 0,  4);
    R(1)  = bus_read(bus, frame + 4,  4);
    R(2)  = bus_read(bus, frame + 8,  4);
    R(3)  = bus_read(bus, frame + 12, 4);
    R(12) = bus_read(bus, frame + 16, 4);
    R(LR) = bus_read(bus, frame + 20, 4);
    R(PC) = bus_read(bus, frame + 24, 4);
    XPSR  = bus_read(bus, frame + 28, 4);
    R(SP) += 32;

    /* Complete the IRQ */
    if (core->state.current_irq > 0) {
        nvic_complete(nvic, core->state.current_irq - 1);
    }
    core->state.current_irq = 0;
}

/* ======================================================================
 * Instruction handlers
 * Each returns STATUS_OK or an error.
 * PC is NOT auto-advanced; handlers set pc_advanced=0 if they want
 * the main loop to increment PC by 2.
 * ====================================================================== */

typedef Status (*InstrHandler)(Core* core, uint16_t instr);

/* We use a flag to track if the handler already updated PC */
static int pc_written;

/* --- Format 1: Shift by immediate --- */

static Status exec_lsl_imm(Core* core, uint16_t instr)
{
    uint8_t rd    = instr & 0x7;
    uint8_t rs    = (instr >> 3) & 0x7;
    uint8_t imm5  = (instr >> 6) & 0x1F;

    if (imm5 == 0) {
        R(rd) = R(rs);
        update_nz(core, R(rd));
    } else {
        set_flag(core, XPSR_C, (R(rs) >> (32 - imm5)) & 1);
        R(rd) = R(rs) << imm5;
        update_nz(core, R(rd));
    }
    return STATUS_OK;
}

static Status exec_lsr_imm(Core* core, uint16_t instr)
{
    uint8_t rd   = instr & 0x7;
    uint8_t rs   = (instr >> 3) & 0x7;
    uint8_t imm5 = (instr >> 6) & 0x1F;

    if (imm5 == 0) imm5 = 32;
    if (imm5 == 32) {
        set_flag(core, XPSR_C, (R(rs) >> 31) & 1);
        R(rd) = 0;
    } else {
        set_flag(core, XPSR_C, (R(rs) >> (imm5 - 1)) & 1);
        R(rd) = R(rs) >> imm5;
    }
    update_nz(core, R(rd));
    return STATUS_OK;
}

static Status exec_asr_imm(Core* core, uint16_t instr)
{
    uint8_t rd   = instr & 0x7;
    uint8_t rs   = (instr >> 3) & 0x7;
    uint8_t imm5 = (instr >> 6) & 0x1F;

    if (imm5 == 0) imm5 = 32;
    if (imm5 == 32) {
        int bit31 = (R(rs) >> 31) & 1;
        set_flag(core, XPSR_C, bit31);
        R(rd) = bit31 ? 0xFFFFFFFF : 0;
    } else {
        set_flag(core, XPSR_C, (R(rs) >> (imm5 - 1)) & 1);
        R(rd) = (uint32_t)((int32_t)R(rs) >> imm5);
    }
    update_nz(core, R(rd));
    return STATUS_OK;
}

/* --- Format 2: Add/subtract register and 3-bit immediate --- */

static Status exec_add_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rn = (instr >> 3) & 0x7;
    uint8_t rm = (instr >> 6) & 0x7;
    uint32_t result = R(rn) + R(rm);
    update_flags_add(core, R(rn), R(rm), result);
    R(rd) = result;
    return STATUS_OK;
}

static Status exec_sub_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rn = (instr >> 3) & 0x7;
    uint8_t rm = (instr >> 6) & 0x7;
    uint32_t result = R(rn) - R(rm);
    update_flags_sub(core, R(rn), R(rm), result);
    R(rd) = result;
    return STATUS_OK;
}

static Status exec_add_imm3(Core* core, uint16_t instr)
{
    uint8_t rd   = instr & 0x7;
    uint8_t rn   = (instr >> 3) & 0x7;
    uint8_t imm3 = (instr >> 6) & 0x7;
    uint32_t result = R(rn) + imm3;
    update_flags_add(core, R(rn), imm3, result);
    R(rd) = result;
    return STATUS_OK;
}

static Status exec_sub_imm3(Core* core, uint16_t instr)
{
    uint8_t rd   = instr & 0x7;
    uint8_t rn   = (instr >> 3) & 0x7;
    uint8_t imm3 = (instr >> 6) & 0x7;
    uint32_t result = R(rn) - imm3;
    update_flags_sub(core, R(rn), imm3, result);
    R(rd) = result;
    return STATUS_OK;
}

/* --- Format 3: Move/Compare/Add/Subtract immediate 8-bit --- */

static Status exec_mov_imm(Core* core, uint16_t instr)
{
    uint8_t rd   = (instr >> 8) & 0x7;
    uint8_t imm8 = instr & 0xFF;
    R(rd) = imm8;
    update_nz(core, R(rd));
    return STATUS_OK;
}

static Status exec_cmp_imm(Core* core, uint16_t instr)
{
    uint8_t rn   = (instr >> 8) & 0x7;
    uint8_t imm8 = instr & 0xFF;
    uint32_t result = R(rn) - imm8;
    update_flags_sub(core, R(rn), imm8, result);
    return STATUS_OK;
}

static Status exec_add_imm8(Core* core, uint16_t instr)
{
    uint8_t rd   = (instr >> 8) & 0x7;
    uint8_t imm8 = instr & 0xFF;
    uint32_t result = R(rd) + imm8;
    update_flags_add(core, R(rd), imm8, result);
    R(rd) = result;
    return STATUS_OK;
}

static Status exec_sub_imm8(Core* core, uint16_t instr)
{
    uint8_t rd   = (instr >> 8) & 0x7;
    uint8_t imm8 = instr & 0xFF;
    uint32_t result = R(rd) - imm8;
    update_flags_sub(core, R(rd), imm8, result);
    R(rd) = result;
    return STATUS_OK;
}

/* --- Format 4: ALU operations (register-register) --- */

static Status exec_and(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    R(rd) &= R(rm);
    update_nz(core, R(rd));
    return STATUS_OK;
}

static Status exec_eor(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    R(rd) ^= R(rm);
    update_nz(core, R(rd));
    return STATUS_OK;
}

static Status exec_lsl_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rs = (instr >> 3) & 0x7;
    uint8_t shift = R(rs) & 0xFF;
    if (shift == 0) {
        /* No change */
    } else if (shift < 32) {
        set_flag(core, XPSR_C, (R(rd) >> (32 - shift)) & 1);
        R(rd) <<= shift;
    } else if (shift == 32) {
        set_flag(core, XPSR_C, R(rd) & 1);
        R(rd) = 0;
    } else {
        set_flag(core, XPSR_C, 0);
        R(rd) = 0;
    }
    update_nz(core, R(rd));
    return STATUS_OK;
}

static Status exec_lsr_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rs = (instr >> 3) & 0x7;
    uint8_t shift = R(rs) & 0xFF;
    if (shift == 0) {
        /* No change */
    } else if (shift < 32) {
        set_flag(core, XPSR_C, (R(rd) >> (shift - 1)) & 1);
        R(rd) >>= shift;
    } else if (shift == 32) {
        set_flag(core, XPSR_C, (R(rd) >> 31) & 1);
        R(rd) = 0;
    } else {
        set_flag(core, XPSR_C, 0);
        R(rd) = 0;
    }
    update_nz(core, R(rd));
    return STATUS_OK;
}

static Status exec_asr_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rs = (instr >> 3) & 0x7;
    uint8_t shift = R(rs) & 0xFF;
    if (shift == 0) {
        /* No change */
    } else if (shift < 32) {
        set_flag(core, XPSR_C, (R(rd) >> (shift - 1)) & 1);
        R(rd) = (uint32_t)((int32_t)R(rd) >> shift);
    } else {
        int bit31 = (R(rd) >> 31) & 1;
        set_flag(core, XPSR_C, bit31);
        R(rd) = bit31 ? 0xFFFFFFFF : 0;
    }
    update_nz(core, R(rd));
    return STATUS_OK;
}

static Status exec_adc(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    uint32_t carry = (XPSR & XPSR_C) ? 1 : 0;
    uint64_t result = (uint64_t)R(rd) + (uint64_t)R(rm) + carry;
    uint32_t res32 = (uint32_t)result;
    update_nz(core, res32);
    set_flag(core, XPSR_C, result > 0xFFFFFFFFU);
    set_flag(core, XPSR_V, (~(R(rd) ^ R(rm)) & (R(rd) ^ res32)) >> 31);
    R(rd) = res32;
    return STATUS_OK;
}

static Status exec_sbc(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    uint32_t carry = (XPSR & XPSR_C) ? 1 : 0;
    uint32_t result = R(rd) - R(rm) - (1 - carry);
    update_nz(core, result);
    /* Carry (no borrow) if Rd >= Rm + (1 - carry) */
    uint64_t sum = (uint64_t)R(rm) + (1 - carry);
    set_flag(core, XPSR_C, (uint64_t)R(rd) >= sum);
    set_flag(core, XPSR_V, ((R(rd) ^ R(rm)) & (R(rd) ^ result)) >> 31);
    R(rd) = result;
    return STATUS_OK;
}

static Status exec_ror(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rs = (instr >> 3) & 0x7;
    uint8_t shift = R(rs) & 0xFF;
    if (shift == 0) {
        /* No change */
    } else {
        shift &= 0x1F;
        if (shift == 0) {
            set_flag(core, XPSR_C, (R(rd) >> 31) & 1);
        } else {
            set_flag(core, XPSR_C, (R(rd) >> (shift - 1)) & 1);
            R(rd) = (R(rd) >> shift) | (R(rd) << (32 - shift));
        }
    }
    update_nz(core, R(rd));
    return STATUS_OK;
}

static Status exec_tst(Core* core, uint16_t instr)
{
    uint8_t rn = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    update_nz(core, R(rn) & R(rm));
    return STATUS_OK;
}

static Status exec_neg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    uint32_t result = 0 - R(rm);
    update_flags_sub(core, 0, R(rm), result);
    R(rd) = result;
    return STATUS_OK;
}

static Status exec_cmp_reg(Core* core, uint16_t instr)
{
    uint8_t rn = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    uint32_t result = R(rn) - R(rm);
    update_flags_sub(core, R(rn), R(rm), result);
    return STATUS_OK;
}

static Status exec_cmn(Core* core, uint16_t instr)
{
    uint8_t rn = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    uint32_t result = R(rn) + R(rm);
    update_flags_add(core, R(rn), R(rm), result);
    return STATUS_OK;
}

static Status exec_orr(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    R(rd) |= R(rm);
    update_nz(core, R(rd));
    return STATUS_OK;
}

static Status exec_mul(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    R(rd) *= R(rm);
    update_nz(core, R(rd));
    return STATUS_OK;
}

static Status exec_bic(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    R(rd) &= ~R(rm);
    update_nz(core, R(rd));
    return STATUS_OK;
}

static Status exec_mvn(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rm = (instr >> 3) & 0x7;
    R(rd) = ~R(rm);
    update_nz(core, R(rd));
    return STATUS_OK;
}

/* --- Format 5: Hi register operations / BX --- */

static Status exec_add_hireg(Core* core, uint16_t instr)
{
    uint8_t rd = ((instr >> 4) & 0x8) | (instr & 0x7);
    uint8_t rm = (instr >> 3) & 0xF;
    R(rd) += R(rm);
    if (rd == PC) {
        R(PC) &= ~1U;
        pc_written = 1;
    }
    return STATUS_OK;
}

static Status exec_cmp_hireg(Core* core, uint16_t instr)
{
    uint8_t rn = ((instr >> 4) & 0x8) | (instr & 0x7);
    uint8_t rm = (instr >> 3) & 0xF;
    uint32_t result = R(rn) - R(rm);
    update_flags_sub(core, R(rn), R(rm), result);
    return STATUS_OK;
}

static Status exec_mov_hireg(Core* core, uint16_t instr)
{
    uint8_t rd = ((instr >> 4) & 0x8) | (instr & 0x7);
    uint8_t rm = (instr >> 3) & 0xF;
    R(rd) = R(rm);
    if (rd == PC) {
        R(PC) &= ~1U;
        pc_written = 1;
    }
    return STATUS_OK;
}

static Status exec_bx(Core* core, uint16_t instr)
{
    uint8_t rm = (instr >> 3) & 0xF;
    uint32_t target = R(rm);

    /* Check for exception return */
    if (IS_EXC_RETURN(target)) {
        exit_exception(core);
        pc_written = 1;
        return STATUS_OK;
    }

    R(PC) = target & ~1U;
    pc_written = 1;
    return STATUS_OK;
}

/* --- Format 6: PC-relative load --- */

static Status exec_ldr_pc(Core* core, uint16_t instr)
{
    uint8_t  rd   = (instr >> 8) & 0x7;
    uint32_t imm8 = instr & 0xFF;
    /* PC is aligned down to word and offset is imm8 * 4 */
    uint32_t base = (R(PC) + 4) & ~3U;
    uint32_t addr = base + (imm8 << 2);
    R(rd) = bus_read(core->bus, addr, 4);
    return STATUS_OK;
}

/* --- Format 7: Load/store with register offset --- */

static Status exec_str_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rn = (instr >> 3) & 0x7;
    uint8_t rm = (instr >> 6) & 0x7;
    bus_write(core->bus, R(rn) + R(rm), R(rd), 4);
    return STATUS_OK;
}

static Status exec_strb_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rn = (instr >> 3) & 0x7;
    uint8_t rm = (instr >> 6) & 0x7;
    bus_write(core->bus, R(rn) + R(rm), R(rd) & 0xFF, 1);
    return STATUS_OK;
}

static Status exec_ldr_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rn = (instr >> 3) & 0x7;
    uint8_t rm = (instr >> 6) & 0x7;
    R(rd) = bus_read(core->bus, R(rn) + R(rm), 4);
    return STATUS_OK;
}

static Status exec_ldrb_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rn = (instr >> 3) & 0x7;
    uint8_t rm = (instr >> 6) & 0x7;
    R(rd) = bus_read(core->bus, R(rn) + R(rm), 1);
    return STATUS_OK;
}

static Status exec_strh_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rn = (instr >> 3) & 0x7;
    uint8_t rm = (instr >> 6) & 0x7;
    bus_write(core->bus, R(rn) + R(rm), R(rd) & 0xFFFF, 2);
    return STATUS_OK;
}

static Status exec_ldrh_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rn = (instr >> 3) & 0x7;
    uint8_t rm = (instr >> 6) & 0x7;
    R(rd) = bus_read(core->bus, R(rn) + R(rm), 2);
    return STATUS_OK;
}

static Status exec_ldrsb_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rn = (instr >> 3) & 0x7;
    uint8_t rm = (instr >> 6) & 0x7;
    uint8_t val = (uint8_t)bus_read(core->bus, R(rn) + R(rm), 1);
    R(rd) = (uint32_t)(int32_t)(int8_t)val;
    return STATUS_OK;
}

static Status exec_ldrsh_reg(Core* core, uint16_t instr)
{
    uint8_t rd = instr & 0x7;
    uint8_t rn = (instr >> 3) & 0x7;
    uint8_t rm = (instr >> 6) & 0x7;
    uint16_t val = (uint16_t)bus_read(core->bus, R(rn) + R(rm), 2);
    R(rd) = (uint32_t)(int32_t)(int16_t)val;
    return STATUS_OK;
}

/* --- Format 9: Load/store with immediate offset (word/byte) --- */

static Status exec_str_imm(Core* core, uint16_t instr)
{
    uint8_t  rd   = instr & 0x7;
    uint8_t  rn   = (instr >> 3) & 0x7;
    uint8_t  imm5 = (instr >> 6) & 0x1F;
    bus_write(core->bus, R(rn) + (imm5 << 2), R(rd), 4);
    return STATUS_OK;
}

static Status exec_ldr_imm(Core* core, uint16_t instr)
{
    uint8_t  rd   = instr & 0x7;
    uint8_t  rn   = (instr >> 3) & 0x7;
    uint8_t  imm5 = (instr >> 6) & 0x1F;
    R(rd) = bus_read(core->bus, R(rn) + (imm5 << 2), 4);
    return STATUS_OK;
}

static Status exec_strb_imm(Core* core, uint16_t instr)
{
    uint8_t  rd   = instr & 0x7;
    uint8_t  rn   = (instr >> 3) & 0x7;
    uint8_t  imm5 = (instr >> 6) & 0x1F;
    bus_write(core->bus, R(rn) + imm5, R(rd) & 0xFF, 1);
    return STATUS_OK;
}

static Status exec_ldrb_imm(Core* core, uint16_t instr)
{
    uint8_t  rd   = instr & 0x7;
    uint8_t  rn   = (instr >> 3) & 0x7;
    uint8_t  imm5 = (instr >> 6) & 0x1F;
    R(rd) = bus_read(core->bus, R(rn) + imm5, 1);
    return STATUS_OK;
}

/* --- Format 10: Load/store halfword with immediate offset --- */

static Status exec_strh_imm(Core* core, uint16_t instr)
{
    uint8_t  rd   = instr & 0x7;
    uint8_t  rn   = (instr >> 3) & 0x7;
    uint8_t  imm5 = (instr >> 6) & 0x1F;
    bus_write(core->bus, R(rn) + (imm5 << 1), R(rd) & 0xFFFF, 2);
    return STATUS_OK;
}

static Status exec_ldrh_imm(Core* core, uint16_t instr)
{
    uint8_t  rd   = instr & 0x7;
    uint8_t  rn   = (instr >> 3) & 0x7;
    uint8_t  imm5 = (instr >> 6) & 0x1F;
    R(rd) = bus_read(core->bus, R(rn) + (imm5 << 1), 2);
    return STATUS_OK;
}

/* --- Format 11: SP-relative load/store --- */

static Status exec_str_sp(Core* core, uint16_t instr)
{
    uint8_t  rd   = (instr >> 8) & 0x7;
    uint32_t imm8 = instr & 0xFF;
    bus_write(core->bus, R(SP) + (imm8 << 2), R(rd), 4);
    return STATUS_OK;
}

static Status exec_ldr_sp(Core* core, uint16_t instr)
{
    uint8_t  rd   = (instr >> 8) & 0x7;
    uint32_t imm8 = instr & 0xFF;
    R(rd) = bus_read(core->bus, R(SP) + (imm8 << 2), 4);
    return STATUS_OK;
}

/* --- Format 12: Load address (ADR / ADD Rd, SP, imm) --- */

static Status exec_adr(Core* core, uint16_t instr)
{
    uint8_t  rd   = (instr >> 8) & 0x7;
    uint32_t imm8 = instr & 0xFF;
    R(rd) = ((R(PC) + 4) & ~3U) + (imm8 << 2);
    return STATUS_OK;
}

static Status exec_add_sp_imm_rd(Core* core, uint16_t instr)
{
    uint8_t  rd   = (instr >> 8) & 0x7;
    uint32_t imm8 = instr & 0xFF;
    R(rd) = R(SP) + (imm8 << 2);
    return STATUS_OK;
}

/* --- Format 13: Adjust SP --- */

static Status exec_adjust_sp(Core* core, uint16_t instr)
{
    uint32_t imm7 = (instr & 0x7F) << 2;
    if (instr & 0x80) {
        R(SP) -= imm7;  /* SUB SP, #imm */
    } else {
        R(SP) += imm7;  /* ADD SP, #imm */
    }
    return STATUS_OK;
}

/* --- Format 14: PUSH / POP --- */

static Status exec_push(Core* core, uint16_t instr)
{
    uint8_t rlist = instr & 0xFF;
    int     store_lr = (instr >> 8) & 1;
    int     count = 0;

    /* Count registers to push */
    for (int i = 0; i < 8; i++)
        if (rlist & (1 << i)) count++;
    if (store_lr) count++;

    R(SP) -= count * 4;
    uint32_t addr = R(SP);

    for (int i = 0; i < 8; i++) {
        if (rlist & (1 << i)) {
            bus_write(core->bus, addr, R(i), 4);
            addr += 4;
        }
    }
    if (store_lr) {
        bus_write(core->bus, addr, R(LR), 4);
    }
    return STATUS_OK;
}

static Status exec_pop(Core* core, uint16_t instr)
{
    uint8_t rlist   = instr & 0xFF;
    int     load_pc = (instr >> 8) & 1;
    uint32_t addr   = R(SP);

    for (int i = 0; i < 8; i++) {
        if (rlist & (1 << i)) {
            R(i) = bus_read(core->bus, addr, 4);
            addr += 4;
        }
    }
    if (load_pc) {
        uint32_t val = bus_read(core->bus, addr, 4);
        addr += 4;

        /* Check for exception return */
        if (IS_EXC_RETURN(val)) {
            R(SP) = addr;
            exit_exception(core);
            pc_written = 1;
            return STATUS_OK;
        }

        R(PC) = val & ~1U;
        pc_written = 1;
    }
    R(SP) = addr;
    return STATUS_OK;
}

/* --- Format 16: Conditional branch --- */

static Status exec_b_cond(Core* core, uint16_t instr)
{
    uint8_t cond = (instr >> 8) & 0xF;
    if (cond == 0xF) {
        /* SVC (supervisor call) - not fully implemented */
        return STATUS_OK;
    }
    if (cond == 0xE) {
        /* Undefined in some revisions; treat as always */
    }
    if (condition_passed(core, cond)) {
        int32_t offset = sign_extend(instr & 0xFF, 8) << 1;
        R(PC) = R(PC) + 4 + offset;
        pc_written = 1;
    }
    return STATUS_OK;
}

/* --- Format 17: SVC --- */

static Status exec_svc(Core* core, uint16_t instr)
{
    (void)core; (void)instr;
    /* Supervisor call - minimal implementation */
    fprintf(stderr, "SVC #%u called\n", instr & 0xFF);
    return STATUS_OK;
}

/* --- Format 18: Unconditional branch --- */

static Status exec_b(Core* core, uint16_t instr)
{
    int32_t offset = sign_extend(instr & 0x7FF, 11) << 1;
    R(PC) = R(PC) + 4 + offset;
    pc_written = 1;
    return STATUS_OK;
}

/* --- NOP --- */

static Status exec_nop(Core* core, uint16_t instr)
{
    (void)core; (void)instr;
    return STATUS_OK;
}

/* ======================================================================
 * Instruction dispatch table
 * Order matters: more specific patterns (larger masks) come first.
 * ====================================================================== */

typedef struct {
    uint16_t     mask;
    uint16_t     pattern;
    InstrHandler handler;
    const char*  name;
} InstrEntry;

static const InstrEntry instr_table[] = {
    /* NOP (very specific) */
    {0xFFFF, 0xBF00, exec_nop,        "NOP"},

    /* Format 4: ALU operations (mask 0xFFC0) */
    {0xFFC0, 0x4000, exec_and,        "AND"},
    {0xFFC0, 0x4040, exec_eor,        "EOR"},
    {0xFFC0, 0x4080, exec_lsl_reg,    "LSL"},
    {0xFFC0, 0x40C0, exec_lsr_reg,    "LSR"},
    {0xFFC0, 0x4100, exec_asr_reg,    "ASR"},
    {0xFFC0, 0x4140, exec_adc,        "ADC"},
    {0xFFC0, 0x4180, exec_sbc,        "SBC"},
    {0xFFC0, 0x41C0, exec_ror,        "ROR"},
    {0xFFC0, 0x4200, exec_tst,        "TST"},
    {0xFFC0, 0x4240, exec_neg,        "NEG"},
    {0xFFC0, 0x4280, exec_cmp_reg,    "CMP"},
    {0xFFC0, 0x42C0, exec_cmn,        "CMN"},
    {0xFFC0, 0x4300, exec_orr,        "ORR"},
    {0xFFC0, 0x4340, exec_mul,        "MUL"},
    {0xFFC0, 0x4380, exec_bic,        "BIC"},
    {0xFFC0, 0x43C0, exec_mvn,        "MVN"},

    /* Format 5: Hi register ops / BX (mask 0xFF00 / 0xFF80) */
    {0xFF80, 0x4700, exec_bx,         "BX"},
    {0xFF00, 0x4400, exec_add_hireg,  "ADD hi"},
    {0xFF00, 0x4500, exec_cmp_hireg,  "CMP hi"},
    {0xFF00, 0x4600, exec_mov_hireg,  "MOV hi"},

    /* SVC (must come before conditional branch) */
    {0xFF00, 0xDF00, exec_svc,        "SVC"},

    /* Format 13: Adjust SP */
    {0xFF00, 0xB000, exec_adjust_sp,  "ADD/SUB SP"},

    /* Format 2: Add/subtract register and imm3 (mask 0xFE00) */
    {0xFE00, 0x1800, exec_add_reg,    "ADD reg"},
    {0xFE00, 0x1A00, exec_sub_reg,    "SUB reg"},
    {0xFE00, 0x1C00, exec_add_imm3,   "ADD imm3"},
    {0xFE00, 0x1E00, exec_sub_imm3,   "SUB imm3"},

    /* Format 7/8: Load/store register offset */
    {0xFE00, 0x5000, exec_str_reg,    "STR reg"},
    {0xFE00, 0x5200, exec_strh_reg,   "STRH reg"},
    {0xFE00, 0x5400, exec_strb_reg,   "STRB reg"},
    {0xFE00, 0x5600, exec_ldrsb_reg,  "LDRSB reg"},
    {0xFE00, 0x5800, exec_ldr_reg,    "LDR reg"},
    {0xFE00, 0x5A00, exec_ldrh_reg,   "LDRH reg"},
    {0xFE00, 0x5C00, exec_ldrb_reg,   "LDRB reg"},
    {0xFE00, 0x5E00, exec_ldrsh_reg,  "LDRSH reg"},

    /* Format 14: PUSH / POP */
    {0xFE00, 0xB400, exec_push,       "PUSH"},
    {0xFE00, 0xBC00, exec_pop,        "POP"},

    /* Format 1: Shift by immediate */
    {0xF800, 0x0000, exec_lsl_imm,    "LSL imm"},
    {0xF800, 0x0800, exec_lsr_imm,    "LSR imm"},
    {0xF800, 0x1000, exec_asr_imm,    "ASR imm"},

    /* Format 3: Move/Compare/Add/Subtract immediate */
    {0xF800, 0x2000, exec_mov_imm,    "MOV imm"},
    {0xF800, 0x2800, exec_cmp_imm,    "CMP imm"},
    {0xF800, 0x3000, exec_add_imm8,   "ADD imm8"},
    {0xF800, 0x3800, exec_sub_imm8,   "SUB imm8"},

    /* Format 6: PC-relative load */
    {0xF800, 0x4800, exec_ldr_pc,     "LDR PC"},

    /* Format 9: Load/store immediate offset (word/byte) */
    {0xF800, 0x6000, exec_str_imm,    "STR imm"},
    {0xF800, 0x6800, exec_ldr_imm,    "LDR imm"},
    {0xF800, 0x7000, exec_strb_imm,   "STRB imm"},
    {0xF800, 0x7800, exec_ldrb_imm,   "LDRB imm"},

    /* Format 10: Load/store halfword immediate offset */
    {0xF800, 0x8000, exec_strh_imm,   "STRH imm"},
    {0xF800, 0x8800, exec_ldrh_imm,   "LDRH imm"},

    /* Format 11: SP-relative load/store */
    {0xF800, 0x9000, exec_str_sp,     "STR SP"},
    {0xF800, 0x9800, exec_ldr_sp,     "LDR SP"},

    /* Format 12: Load address */
    {0xF800, 0xA000, exec_adr,        "ADR"},
    {0xF800, 0xA800, exec_add_sp_imm_rd, "ADD SP imm"},

    /* Format 16: Conditional branch */
    {0xF000, 0xD000, exec_b_cond,     "B<cond>"},

    /* Format 18: Unconditional branch */
    {0xF800, 0xE000, exec_b,          "B"},

    /* Sentinel */
    {0, 0, NULL, NULL}
};

/* ======================================================================
 * 32-bit instruction handling (BL only for now)
 * ====================================================================== */

static Status execute_32bit(Core* core, uint16_t hw1, uint16_t hw2)
{
    /* BL: hw1 = 11110 S imm10, hw2 = 11 J1 1 J2 imm11 */
    if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0xD000) {
        uint32_t S    = (hw1 >> 10) & 1;
        uint32_t J1   = (hw2 >> 13) & 1;
        uint32_t J2   = (hw2 >> 11) & 1;
        uint32_t I1   = ~(J1 ^ S) & 1;
        uint32_t I2   = ~(J2 ^ S) & 1;
        uint32_t imm10 = hw1 & 0x3FF;
        uint32_t imm11 = hw2 & 0x7FF;

        uint32_t raw = (S << 24) | (I1 << 23) | (I2 << 22)
                      | (imm10 << 12) | (imm11 << 1);
        int32_t offset = sign_extend(raw, 25);

        R(LR) = (R(PC) + 4) | 1;  /* Return address with Thumb bit */
        R(PC) = R(PC) + 4 + offset;
        pc_written = 1;
        return STATUS_OK;
    }

    fprintf(stderr, "Unimplemented 32-bit instruction: 0x%04X 0x%04X at PC=0x%08X\n",
            hw1, hw2, R(PC));
    return STATUS_INVALID_INSTRUCTION;
}

/* ======================================================================
 * Public API
 * ====================================================================== */

void core_init(Core* core, Bus* bus, void* nvic)
{
    memset(&core->state, 0, sizeof(CoreState));
    core->bus  = bus;
    core->nvic = nvic;
    core->state.thumb_mode    = 1;
    core->state.interruptible = 1;
}

void core_reset(Core* core)
{
    Bus* bus = core->bus;
    void* nvic = core->nvic;

    memset(&core->state, 0, sizeof(CoreState));
    core->state.thumb_mode    = 1;
    core->state.interruptible = 1;

    /* Vector table at 0x00000000 (aliased to Flash) */
    core->state.r[SP] = bus_read(bus, 0x00000000, 4);
    core->state.r[PC] = bus_read(bus, 0x00000004, 4) & ~1U;

    core->bus  = bus;
    core->nvic = nvic;
}

Status core_step(Core* core)
{
    NVIC* nvic = (NVIC*)core->nvic;
    uint32_t pc = R(PC);

    /* Fetch instruction (16-bit) */
    uint16_t instr = (uint16_t)bus_read(core->bus, pc, 2);

    pc_written = 0;

    /* Check for 32-bit instruction */
    if ((instr & 0xE000) == 0xE000 && (instr & 0x1800) != 0) {
        uint16_t hw2 = (uint16_t)bus_read(core->bus, pc + 2, 2);
        Status s = execute_32bit(core, instr, hw2);
        if (s != STATUS_OK) return s;
        if (!pc_written) R(PC) += 4;
    } else {
        /* Look up in instruction table */
        const InstrEntry* entry = instr_table;
        Status s = STATUS_INVALID_INSTRUCTION;

        while (entry->handler != NULL) {
            if ((instr & entry->mask) == entry->pattern) {
                s = entry->handler(core, instr);
                break;
            }
            entry++;
        }

        if (entry->handler == NULL) {
            fprintf(stderr, "Unknown instruction 0x%04X at PC=0x%08X\n", instr, pc);
            return STATUS_INVALID_INSTRUCTION;
        }

        if (s != STATUS_OK) return s;
        if (!pc_written) R(PC) += 2;
    }

    CYCLES++;

    /* Check for pending interrupts */
    if (core->state.interruptible) {
        uint32_t irq;
        if (nvic_get_pending_irq(nvic, &irq)) {
            enter_exception(core, irq);
        }
    }

    return STATUS_OK;
}

const CoreState* core_get_state(const Core* core)
{
    return &core->state;
}
