#include "tests/test.h"
#include "core/core.h"
#include "bus/bus.h"
#include "nvic/nvic.h"
#include "memory/memory.h"
#include <string.h>

/* --- Shared test environment for core tests --- */
static Memory  t_mem;
static NVIC    t_nvic;
static Bus     t_bus;
static Core    t_core;

static void setup_core(void)
{
    memory_init(&t_mem);
    nvic_init(&t_nvic);
    bus_init(&t_bus);

    bus_register_region(&t_bus, 0x00000000, FLASH_SIZE,
                        &t_mem, memory_flash_read, memory_flash_write);
    bus_register_region(&t_bus, FLASH_BASE, FLASH_SIZE,
                        &t_mem, memory_flash_read, memory_flash_write);
    bus_register_region(&t_bus, SRAM_BASE, SRAM_SIZE,
                        &t_mem, memory_sram_read, memory_sram_write);

    core_init(&t_core, &t_bus, &t_nvic);
}

/**
 * Load instructions into flash starting at given offset.
 * Also sets up a minimal vector table pointing SP to top of SRAM
 * and reset handler to the code address.
 */
static void load_code(uint32_t code_offset, const uint16_t* instrs, int count)
{
    /* Vector table */
    flash_write32(&t_mem, 0, 0x20005000);  /* SP */
    flash_write32(&t_mem, 4, FLASH_BASE + code_offset + 1);  /* Reset + Thumb bit */

    for (int i = 0; i < count; i++) {
        flash_write16(&t_mem, code_offset + i * 2, instrs[i]);
    }

    core_reset(&t_core);
}

/* === Tests === */

/* --- Test: MOV immediate --- */
static void test_core_mov_imm(void)
{
    setup_core();
    uint16_t code[] = {
        0x2042,  /* MOV R0, #0x42 */
        0x21FF,  /* MOV R1, #0xFF */
        0xE7FE,  /* B . (loop) */
    };
    load_code(0x80, code, 3);

    ASSERT_EQ(core_step(&t_core), STATUS_OK);
    ASSERT_EQ(t_core.state.r[0], 0x42);

    ASSERT_EQ(core_step(&t_core), STATUS_OK);
    ASSERT_EQ(t_core.state.r[1], 0xFF);
}

/* --- Test: ADD / SUB with flags --- */
static void test_core_add_sub(void)
{
    setup_core();
    uint16_t code[] = {
        0x2064,  /* MOV R0, #100 */
        0x2132,  /* MOV R1, #50 */
        0x1842,  /* ADD R2, R0, R1    → R2 = 150 */
        0x1A83,  /* SUB R3, R0, R2    → R3 = 100 - 150 = -50 (underflow) */
        0xE7FE,  /* B . */
    };
    load_code(0x80, code, 5);

    core_step(&t_core); /* MOV R0, #100 */
    core_step(&t_core); /* MOV R1, #50 */
    core_step(&t_core); /* ADD R2, R0, R1 */
    ASSERT_EQ(t_core.state.r[2], 150);

    core_step(&t_core); /* SUB R3, R0, R2 → 100 - 150 */
    ASSERT_EQ(t_core.state.r[3], (uint32_t)(-50));
    /* N flag should be set (negative result) */
    ASSERT(t_core.state.xpsr & XPSR_N);
    /* C flag should be clear (borrow occurred: 100 < 150) */
    ASSERT(!(t_core.state.xpsr & XPSR_C));
}

/* --- Test: CMP + BEQ (branch taken) --- */
static void test_core_cmp_beq_taken(void)
{
    setup_core();
    uint16_t code[] = {
        0x200A,  /* MOV R0, #10 */
        0x210A,  /* MOV R1, #10 */
        0x4288,  /* CMP R0, R1 */
        0xD000,  /* BEQ +0 (target = PC+4+0 = skip next instr) */
        0x22FF,  /* MOV R2, #0xFF (should be SKIPPED) */
        0x2301,  /* MOV R3, #1 */
        0xE7FE,  /* B . */
    };
    load_code(0x80, code, 7);

    core_step(&t_core); /* MOV R0, #10 */
    core_step(&t_core); /* MOV R1, #10 */
    core_step(&t_core); /* CMP R0, R1 → Z=1 */
    ASSERT(t_core.state.xpsr & XPSR_Z);

    core_step(&t_core); /* BEQ → should branch (Z=1) */
    /* PC should skip over MOV R2, #0xFF */
    core_step(&t_core); /* MOV R3, #1 */
    ASSERT_EQ(t_core.state.r[3], 1);
    ASSERT_EQ(t_core.state.r[2], 0);  /* R2 was skipped */
}

/* --- Test: CMP + BNE (branch not taken) --- */
static void test_core_cmp_bne_not_taken(void)
{
    setup_core();
    uint16_t code[] = {
        0x200A,  /* MOV R0, #10 */
        0x210A,  /* MOV R1, #10 */
        0x4288,  /* CMP R0, R1 */
        0xD101,  /* BNE +2 (skip next if not equal) */
        0x22AA,  /* MOV R2, #0xAA (should EXECUTE because R0==R1) */
        0xE7FE,  /* B . */
    };
    load_code(0x80, code, 6);

    core_step(&t_core); /* MOV R0 */
    core_step(&t_core); /* MOV R1 */
    core_step(&t_core); /* CMP */
    core_step(&t_core); /* BNE → not taken (Z=1, NE requires Z=0) */
    core_step(&t_core); /* MOV R2, #0xAA → executes */
    ASSERT_EQ(t_core.state.r[2], 0xAA);
}

/* --- Test: LDR / STR via SRAM --- */
static void test_core_ldr_str(void)
{
    setup_core();
    /*
     * STR R0, [R4, #0]  where R4 = SRAM base + 0x100
     * LDR R5, [R4, #0]  → R5 should equal R0
     */
    /*
     * Use PUSH/POP to test store/load via stack (SP is in SRAM).
     * PUSH {R0} stores to SP-4, POP {R1} loads it back.
     */
    uint16_t code2[] = {
        0x2042,  /* MOV R0, #0x42 */
        0xB401,  /* PUSH {R0} → SP -= 4, store R0 at [SP] */
        0xBC02,  /* POP {R1} → R1 = [SP], SP += 4 */
        0xE7FE,  /* B . */
    };
    load_code(0x80, code2, 4);

    core_step(&t_core); /* MOV R0, #0x42 */
    ASSERT_EQ(t_core.state.r[0], 0x42);

    uint32_t sp_before = t_core.state.r[REG_SP];
    core_step(&t_core); /* PUSH {R0} */
    ASSERT_EQ(t_core.state.r[REG_SP], sp_before - 4);

    core_step(&t_core); /* POP {R1} */
    ASSERT_EQ(t_core.state.r[1], 0x42);
    ASSERT_EQ(t_core.state.r[REG_SP], sp_before);
}

/* --- Test: PUSH / POP with multiple registers --- */
static void test_core_push_pop(void)
{
    setup_core();
    uint16_t code[] = {
        0x2001,  /* MOV R0, #1 */
        0x2102,  /* MOV R1, #2 */
        0x2203,  /* MOV R2, #3 */
        0xB407,  /* PUSH {R0, R1, R2} */
        0x2000,  /* MOV R0, #0 (clear) */
        0x2100,  /* MOV R1, #0 */
        0x2200,  /* MOV R2, #0 */
        0xBC07,  /* POP {R0, R1, R2} */
        0xE7FE,  /* B . */
    };
    load_code(0x80, code, 9);

    for (int i = 0; i < 4; i++) core_step(&t_core); /* MOV x3 + PUSH */
    ASSERT_EQ(t_core.state.r[0], 1);  /* R0 unchanged after PUSH */

    for (int i = 0; i < 3; i++) core_step(&t_core); /* Clear R0-R2 */
    ASSERT_EQ(t_core.state.r[0], 0);
    ASSERT_EQ(t_core.state.r[1], 0);
    ASSERT_EQ(t_core.state.r[2], 0);

    core_step(&t_core); /* POP {R0, R1, R2} */
    ASSERT_EQ(t_core.state.r[0], 1);
    ASSERT_EQ(t_core.state.r[1], 2);
    ASSERT_EQ(t_core.state.r[2], 3);
}

/* --- Test: BL (branch with link) + BX LR (return) --- */
static void test_core_bl_bx(void)
{
    setup_core();
    /*
     * Layout:
     *   0x80: BL +8 → call subroutine at 0x8C
     *   0x84: MOV R2, #0xBB  ← return here
     *   0x86: B .
     *   ...
     *   0x8C: MOV R0, #0xAA  ← subroutine
     *   0x8E: BX LR           ← return
     *
     * BL encoding (Thumb-2, 32-bit):
     *   Target = PC + 4 + offset → 0x80 + 4 + offset = 0x8C → offset = 8
     *   offset = 8, S=0, imm10=0, imm11 = (8>>1) = 4
     *   For offset bits I1=0, I2=0: J1 = NOT(0^S) = 1, J2 = 1
     *   hw1 = 0xF000, hw2 = 0xF804
     */
    uint16_t code_at_80[] = {
        0xF000,  /* BL hw1 (at 0x80) */
        0xF804,  /* BL hw2 (at 0x82) */
        0x22BB,  /* MOV R2, #0xBB (at 0x84, return point) */
        0xE7FE,  /* B . (at 0x86) */
    };
    /* Subroutine at 0x8C (offset 0x8C - 0x80 = 0x0C from code start) */
    uint16_t sub_at_8C[] = {
        0x20AA,  /* MOV R0, #0xAA (at 0x8C) */
        0x4770,  /* BX LR (at 0x8E) */
    };

    load_code(0x80, code_at_80, 4);
    flash_write16(&t_mem, 0x8C, sub_at_8C[0]);
    flash_write16(&t_mem, 0x8E, sub_at_8C[1]);

    /* Step 1: BL (32-bit instruction, reads both halfwords) */
    core_step(&t_core);
    /* Should now be at 0x8C with LR = 0x85 (return addr | 1) */
    ASSERT_EQ(t_core.state.r[REG_PC], 0x0800008C);
    ASSERT_EQ(t_core.state.r[REG_LR], 0x08000085);

    /* Step 2: MOV R0, #0xAA */
    core_step(&t_core);
    ASSERT_EQ(t_core.state.r[0], 0xAA);

    /* Step 3: BX LR → return to 0x84 */
    core_step(&t_core);
    ASSERT_EQ(t_core.state.r[REG_PC], 0x08000084);

    /* Step 4: MOV R2, #0xBB */
    core_step(&t_core);
    ASSERT_EQ(t_core.state.r[2], 0xBB);
}

/* --- Test: MUL --- */
static void test_core_mul(void)
{
    setup_core();
    uint16_t code[] = {
        0x2007,  /* MOV R0, #7 */
        0x2106,  /* MOV R1, #6 */
        0x4348,  /* MUL R0, R1 → R0 = R0 * R1 = 42 */
        0xE7FE,  /* B . */
    };
    load_code(0x80, code, 4);

    core_step(&t_core);
    core_step(&t_core);
    core_step(&t_core);
    ASSERT_EQ(t_core.state.r[0], 42);
}

/* --- Test: AND, ORR, EOR --- */
static void test_core_logic(void)
{
    setup_core();
    uint16_t code[] = {
        0x20FF,  /* MOV R0, #0xFF */
        0x210F,  /* MOV R1, #0x0F */
        0x2200,  /* MOV R2, #0 (will copy R0 first) */
        /* AND: need R0 & R1. AND modifies Rd, so copy R0 to R2 first. */
        0x0002,  /* LSL R2, R0, #0 → MOV R2, R0 (R2 = 0xFF) */
        0x400A,  /* AND R2, R1 → R2 = 0xFF & 0x0F = 0x0F */
        0x0003,  /* LSL R3, R0, #0 → MOV R3, R0 */
        0x430B,  /* ORR R3, R1 → R3 = 0xFF | 0x0F = 0xFF */
        0x0004,  /* LSL R4, R0, #0 → MOV R4, R0 */
        0x404C,  /* EOR R4, R1 → R4 = 0xFF ^ 0x0F = 0xF0 */
        0xE7FE,  /* B . */
    };
    load_code(0x80, code, 10);

    for (int i = 0; i < 9; i++) core_step(&t_core);

    ASSERT_EQ(t_core.state.r[2], 0x0F);   /* AND */
    ASSERT_EQ(t_core.state.r[3], 0xFF);   /* ORR */
    ASSERT_EQ(t_core.state.r[4], 0xF0);   /* EOR */
}

/* --- Test: cycle counter increments --- */
static void test_core_cycles(void)
{
    setup_core();
    uint16_t code[] = {
        0xBF00,  /* NOP */
        0xBF00,  /* NOP */
        0xBF00,  /* NOP */
        0xE7FE,  /* B . */
    };
    load_code(0x80, code, 4);

    ASSERT_EQ64(t_core.state.cycles, 0);
    core_step(&t_core);
    ASSERT_EQ64(t_core.state.cycles, 1);
    core_step(&t_core);
    ASSERT_EQ64(t_core.state.cycles, 2);
    core_step(&t_core);
    ASSERT_EQ64(t_core.state.cycles, 3);
}

void test_core_all(void)
{
    TEST_SUITE("Core (ARM Cortex-M3)");
    RUN_TEST(test_core_mov_imm);
    RUN_TEST(test_core_add_sub);
    RUN_TEST(test_core_cmp_beq_taken);
    RUN_TEST(test_core_cmp_bne_not_taken);
    RUN_TEST(test_core_ldr_str);
    RUN_TEST(test_core_push_pop);
    RUN_TEST(test_core_bl_bx);
    RUN_TEST(test_core_mul);
    RUN_TEST(test_core_logic);
    RUN_TEST(test_core_cycles);
}
