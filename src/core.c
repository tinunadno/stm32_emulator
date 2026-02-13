/**
 * @file core.c
 * @brief ARM Cortex-M3 Core Emulator Implementation
 */

#include "core.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * Internal Constants
 * ======================================================================== */

#define FLASH_START    0x08000000
#define FLASH_END      0x0800FFFF
#define SRAM_START     0x20000000
#define SRAM_END       0x20004FFF
#define PERIPH_START   0x40000000
#define PERIPH_END     0x5FFFFFFF

/* Instruction type codes */
#define INST_UNKNOWN   0
#define INST_MOV       1
#define INST_ADD       2
#define INST_SUB       3
#define INST_MUL       4
#define INST_AND       5
#define INST_ORR       6
#define INST_EOR       7
#define INST_LSL       8
#define INST_LSR       9
#define INST_ASR       10
#define INST_ROR       11
#define INST_CMP       12
#define INST_TST       13
#define INST_B         14
#define INST_BL        15
#define INST_BX        16
#define INST_BLX       17
#define INST_LDR       18
#define INST_STR       19
#define INST_LDRH      20
#define INST_STRH      21
#define INST_LDRB      22
#define INST_STRB      23
#define INST_PUSH      24
#define INST_POP       25
#define INST_LDM       26
#define INST_STM       27
#define INST_MRS       28
#define INST_MSR       29
#define INST_SVC       30
#define INST_UDF       31

/* ========================================================================
 * Internal Helper Functions
 * ======================================================================== */

/**
 * @brief Log message if callback is set
 */
static void core_log(Core* core, const char* message) {
    if (core && core->log_callback) {
        core->log_callback(message);
    }
}

/**
 * @brief Update NZCV flags based on result
 */
static void core_update_nzcv_flags(Core* core, uint32_t result, bool carry, bool overflow) {
    bool n = (result & (1U << 31)) != 0;
    bool z = (result == 0);
    
    core->state.xpsr &= ~(XPSR_N_MASK | XPSR_Z_MASK | XPSR_C_MASK | XPSR_V_MASK);
    
    if (n) core->state.xpsr |= XPSR_N_MASK;
    if (z) core->state.xpsr |= XPSR_Z_MASK;
    if (carry) core->state.xpsr |= XPSR_C_MASK;
    if (overflow) core->state.xpsr |= XPSR_V_MASK;
}

/**
 * @brief Add with carry and flags
 */
static uint32_t core_add_with_carry(Core* core, uint32_t a, uint32_t b, bool carry_in, bool update_flags) {
    uint64_t result_64 = (uint64_t)a + b + (carry_in ? 1 : 0);
    uint32_t result = (uint32_t)result_64;
    
    if (update_flags) {
        bool carry_out = result_64 > 0xFFFFFFFF;
        bool overflow = ((~(a ^ b) & (a ^ result)) & (1U << 31)) != 0;
        core_update_nzcv_flags(core, result, carry_out, overflow);
    }
    
    return result;
}

/**
 * @brief Subtract with borrow and flags
 */
static uint32_t core_sub_with_borrow(Core* core, uint32_t a, uint32_t b, bool borrow_in, bool update_flags) {
    uint64_t result_64 = (uint64_t)a - b - (borrow_in ? 1 : 0);
    uint32_t result = (uint32_t)result_64;
    
    if (update_flags) {
        bool borrow_out = (b + (borrow_in ? 1 : 0)) > a;
        bool overflow = ((a ^ b) & (a ^ result)) & (1U << 31);
        core_update_nzcv_flags(core, result, !borrow_out, overflow);
    }
    
    return result;
}

/* ========================================================================
 * Core API Functions
 * ======================================================================== */

bool core_init(Core* core) {
    if (!core) {
        return false;
    }
    
    memset(core, 0, sizeof(Core));
    
    // Initialize state
    core->state.is_thumb_mode = true;
    core->state.interruptible = true;
    core->state.current_irq = 0;
    core->state.cycles_executed = 0;
    core->state.is_running = false;
    core->state.is_halted = false;
    
    // Set Thumb bit in xPSR
    core->state.xpsr = XPSR_T_MASK;
    
    core_log(core, "Core initialized");
    return true;
}

void core_reset(Core* core) {
    if (!core) {
        return;
    }
    
    // Clear all registers
    memset(core->state.registers, 0, sizeof(core->state.registers));
    
    // Reset PC to start of Flash (vector table at 0x08000000)
    // After reset, PC = address of first instruction (vector table[1])
    core->state.registers[REG_PC] = FLASH_START;
    
    // Reset xPSR (Thumb bit set)
    core->state.xpsr = XPSR_T_MASK;
    
    // Reset state
    core->state.is_thumb_mode = true;
    core->state.interruptible = true;
    core->state.current_irq = 0;
    core->state.cycles_executed = 0;
    core->state.is_running = false;
    core->state.is_halted = false;
    
    core_log(core, "Core reset");
}

bool core_step(Core* core) {
    if (!core || core->state.is_halted) {
        return false;
    }
    
    uint32_t pc = core->state.registers[REG_PC];
    
    // Check for interrupts before executing instruction
    if (core->state.interruptible && core_check_interrupt(core)) {
        // Interrupt will be handled in core_check_interrupt
    }
    
    // Fetch instruction
    uint32_t instruction = core_fetch_instruction(core, pc);
    
    // Decode and execute
    uint32_t cycles = core_execute_instruction(core, &(Instruction){
        .opcode = instruction,
        .address = pc,
        .type = core_decode_instruction(instruction),
        .size = 4,  // Cortex-M3 uses 32-bit instructions in Thumb-2
        .is_valid = true
    });
    
    core->state.cycles_executed += cycles;
    
    return true;
}

uint32_t core_step_n(Core* core, uint32_t count) {
    if (!core) {
        return 0;
    }
    
    uint32_t executed = 0;
    for (uint32_t i = 0; i < count && !core->state.is_halted; i++) {
        if (core_step(core)) {
            executed++;
        } else {
            break;
        }
    }
    
    return executed;
}

void core_run(Core* core) {
    if (!core) {
        return;
    }
    
    core->state.is_running = true;
    core->state.is_halted = false;
    core_log(core, "Core running");
}

void core_stop(Core* core) {
    if (!core) {
        return;
    }
    
    core->state.is_running = false;
    core_log(core, "Core stopped");
}

void core_halt(Core* core) {
    if (!core) {
        return;
    }
    
    core->state.is_halted = true;
    core->state.is_running = false;
    core_log(core, "Core halted");
}

void core_resume(Core* core) {
    if (!core) {
        return;
    }
    
    core->state.is_halted = false;
    core_log(core, "Core resumed");
}

/* ========================================================================
 * Register Access Functions
 * ======================================================================== */

uint32_t core_get_register(Core* core, uint8_t reg_num) {
    if (!core || reg_num >= NUM_REGISTERS) {
        return 0;
    }
    
    return core->state.registers[reg_num];
}

bool core_set_register(Core* core, uint8_t reg_num, uint32_t value) {
    if (!core || reg_num >= NUM_REGISTERS) {
        return false;
    }
    
    core->state.registers[reg_num] = value;
    return true;
}

uint32_t core_get_xpsr(Core* core) {
    if (!core) {
        return 0;
    }
    
    return core->state.xpsr;
}

void core_set_xpsr(Core* core, uint32_t value) {
    if (!core) {
        return;
    }
    
    core->state.xpsr = value;
}

void core_get_flags(Core* core, bool* n, bool* z, bool* c, bool* v) {
    if (!core) {
        return;
    }
    
    if (n) *n = (core->state.xpsr & XPSR_N_MASK) != 0;
    if (z) *z = (core->state.xpsr & XPSR_Z_MASK) != 0;
    if (c) *c = (core->state.xpsr & XPSR_C_MASK) != 0;
    if (v) *v = (core->state.xpsr & XPSR_V_MASK) != 0;
}

void core_set_flags(Core* core, bool n, bool z, bool c, bool v) {
    if (!core) {
        return;
    }
    
    core->state.xpsr &= ~(XPSR_N_MASK | XPSR_Z_MASK | XPSR_C_MASK | XPSR_V_MASK);
    
    if (n) core->state.xpsr |= XPSR_N_MASK;
    if (z) core->state.xpsr |= XPSR_Z_MASK;
    if (c) core->state.xpsr |= XPSR_C_MASK;
    if (v) core->state.xpsr |= XPSR_V_MASK;
}

/* ========================================================================
 * State Query Functions
 * ======================================================================== */

uint32_t core_get_pc(Core* core) {
    if (!core) {
        return 0;
    }
    
    return core->state.registers[REG_PC];
}

uint32_t core_get_sp(Core* core) {
    if (!core) {
        return 0;
    }
    
    return core->state.registers[REG_SP];
}

uint32_t core_get_cycle_count(Core* core) {
    if (!core) {
        return 0;
    }
    
    return core->state.cycles_executed;
}

CoreState* core_get_state(Core* core) {
    if (!core) {
        return NULL;
    }
    
    return &core->state;
}

/* ========================================================================
 * Interrupt Handling Functions
 * ======================================================================== */

bool core_check_interrupt(Core* core) {
    if (!core || !core->nvic || !core->state.interruptible) {
        return false;
    }
    
    // Query NVIC for pending interrupts
    // This would call nvic->has_pending_interrupt()
    // For now, return false as placeholder
    return false;
}

void core_enter_interrupt(Core* core, uint32_t irq_number, uint32_t vector_address) {
    if (!core) {
        return;
    }
    
    // Save context
    core_save_context(core);
    
    // Set current IRQ
    core->state.current_irq = irq_number;
    
    // Set LR to exception return value
    core->state.registers[REG_LR] = EXC_RETURN_HANDLER;
    
    // Set PC to vector address
    core->state.registers[REG_PC] = vector_address;
    
    char log_msg[64];
    snprintf(log_msg, sizeof(log_msg), "Entered interrupt IRQ%lu at 0x%08lX",
             (unsigned long)irq_number, (unsigned long)vector_address);
    core_log(core, log_msg);
}

void core_exit_interrupt(Core* core) {
    if (!core) {
        return;
    }
    
    // Restore context
    core_restore_context(core);
    
    // Clear current IRQ
    core->state.current_irq = 0;
    
    core_log(core, "Exited interrupt");
}

void core_enable_interrupts(Core* core) {
    if (!core) {
        return;
    }
    
    core->state.interruptible = true;
    core_log(core, "Interrupts enabled");
}

void core_disable_interrupts(Core* core) {
    if (!core) {
        return;
    }
    
    core->state.interruptible = false;
    core_log(core, "Interrupts disabled");
}

/* ========================================================================
 * Instruction Execution Functions
 * ======================================================================== */

uint32_t core_fetch_instruction(Core* core, uint32_t address) {
    if (!core) {
        return 0;
    }
    
    // Read 16-bit instruction first
    uint16_t inst16 = (uint16_t)core_read_halfword(core, address);
    
    // Check if this is a 32-bit instruction
    // 32-bit Thumb-2 instructions have bits [15:11] = 0b11101, 0b11110, or 0b11111
    if ((inst16 & 0xF800) == 0xE800 || (inst16 & 0xF800) == 0xF000 ||
        (inst16 & 0xF800) == 0xF800) {
        // Read the upper halfword
        uint16_t inst16_hi = (uint16_t)core_read_halfword(core, address + 2);
        // Combine: high halfword in upper 16 bits, low halfword in lower 16 bits
        return ((uint32_t)inst16_hi << 16) | inst16;
    }
    
    // 16-bit instruction - return in lower 16 bits
    return inst16;
}

uint8_t core_decode_instruction(uint32_t instruction) {
    // Simplified instruction decoding
    // In a full implementation, this would decode all Thumb-2 instructions
    
    // Check for 16-bit instructions (upper halfword zero)
    if ((instruction & 0xFFFF0000) == 0) {
        uint16_t inst16 = (uint16_t)instruction;
        
        // MOV Rd, #imm
        if ((inst16 & 0xF800) == 0x2000) {
            return INST_MOV;
        }
        // ADD Rd, Rn, #imm
        if ((inst16 & 0xF800) == 0x3000) {
            return INST_ADD;
        }
        // SUB Rd, Rn, #imm
        if ((inst16 & 0xF800) == 0x3800) {
            return INST_SUB;
        }
        // ADD Rd, Rn, Rm
        if ((inst16 & 0xFE00) == 0x1800) {
            return INST_ADD;
        }
        // SUB Rd, Rn, Rm
        if ((inst16 & 0xFE00) == 0x1A00) {
            return INST_SUB;
        }
        // CMP Rn, Rm
        if ((inst16 & 0xFE00) == 0x4200) {
            return INST_CMP;
        }
        // AND Rd, Rn
        if ((inst16 & 0xFFC0) == 0x4000) {
            return INST_AND;
        }
        // EOR Rd, Rn
        if ((inst16 & 0xFFC0) == 0x4040) {
            return INST_EOR;
        }
        // LSL Rd, Rn
        if ((inst16 & 0xFFC0) == 0x4080) {
            return INST_LSL;
        }
        // LSR Rd, Rn
        if ((inst16 & 0xFFC0) == 0x40C0) {
            return INST_LSR;
        }
        // ASR Rd, Rn
        if ((inst16 & 0xFFC0) == 0x4100) {
            return INST_ASR;
        }
        // ADC Rd, Rn
        if ((inst16 & 0xFFC0) == 0x4140) {
            return INST_ADD;
        }
        // SBC Rd, Rn
        if ((inst16 & 0xFFC0) == 0x4180) {
            return INST_SUB;
        }
        // ROR Rd, Rn
        if ((inst16 & 0xFFC0) == 0x41C0) {
            return INST_ROR;
        }
        // ORR Rd, Rn
        if ((inst16 & 0xFFC0) == 0x4300) {
            return INST_ORR;
        }
        // MUL Rd, Rn
        if ((inst16 & 0xFFC0) == 0x4340) {
            return INST_MUL;
        }
        // BIC Rd, Rn
        if ((inst16 & 0xFFC0) == 0x4380) {
            return INST_AND;
        }
        // MVN Rd, Rn
        if ((inst16 & 0xFFC0) == 0x43C0) {
            return INST_MOV;
        }
        // B cond
        if ((inst16 & 0xF000) == 0xD000) {
            return INST_B;
        }
        // BLX (16-bit)
        if ((inst16 & 0xFF80) == 0x4780) {
            return INST_BLX;
        }
        // BX
        if ((inst16 & 0xFF80) == 0x4700) {
            return INST_BX;
        }
        // PUSH
        if ((inst16 & 0xFE00) == 0xB400) {
            return INST_PUSH;
        }
        // POP
        if ((inst16 & 0xFE00) == 0xBC00) {
            return INST_POP;
        }
        // LDR Rd, [PC, #imm]
        if ((inst16 & 0xF800) == 0x4800) {
            return INST_LDR;
        }
        // LDR Rd, [Rn, #imm]
        if ((inst16 & 0xF800) == 0x6800) {
            return INST_LDR;
        }
        // STR Rd, [Rn, #imm]
        if ((inst16 & 0xF800) == 0x6000) {
            return INST_STR;
        }
        // LDRB Rd, [Rn, #imm]
        if ((inst16 & 0xF800) == 0x7800) {
            return INST_LDRB;
        }
        // STRB Rd, [Rn, #imm]
        if ((inst16 & 0xF800) == 0x7000) {
            return INST_STRB;
        }
        // LDRH Rd, [Rn, #imm]
        if ((inst16 & 0xF800) == 0x8800) {
            return INST_LDRH;
        }
        // STRH Rd, [Rn, #imm]
        if ((inst16 & 0xF800) == 0x8000) {
            return INST_STRH;
        }
        // LDR Rd, [Rn, Rm]
        if ((inst16 & 0xFE00) == 0x5800) {
            return INST_LDR;
        }
        // STR Rd, [Rn, Rm]
        if ((inst16 & 0xFE00) == 0x5000) {
            return INST_STR;
        }
        // LDRB Rd, [Rn, Rm]
        if ((inst16 & 0xFE00) == 0x5C00) {
            return INST_LDRB;
        }
        // STRB Rd, [Rn, Rm]
        if ((inst16 & 0xFE00) == 0x5400) {
            return INST_STRB;
        }
        // LDRH Rd, [Rn, Rm]
        if ((inst16 & 0xFE00) == 0x5A00) {
            return INST_LDRH;
        }
        // STRH Rd, [Rn, Rm]
        if ((inst16 & 0xFE00) == 0x5200) {
            return INST_STRH;
        }
    } else {
        // 32-bit instructions
        uint16_t inst_hi = (uint16_t)(instruction >> 16);
        uint16_t inst_lo = (uint16_t)instruction;
        
        // BL
        if ((inst_hi & 0xF800) == 0xF000 && (inst_lo & 0xD000) == 0xD000) {
            return INST_BL;
        }
        // B.W
        if ((inst_hi & 0xF800) == 0xF000 && (inst_lo & 0xD000) == 0x9000) {
            return INST_B;
        }
        // SVC
        if ((inst_hi & 0xFFE0) == 0xDF00) {
            return INST_SVC;
        }
        // UDF
        if ((inst_hi & 0xFFE0) == 0xDE00) {
            return INST_UDF;
        }
    }
    
    return INST_UNKNOWN;
}

uint32_t core_execute_instruction(Core* core, Instruction* instruction) {
    if (!core || !instruction || !instruction->is_valid) {
        return 1;
    }
    
    uint32_t cycles = 1;
    uint32_t inst = instruction->opcode;
    
    // Check for 16-bit instruction
    if ((inst & 0xFFFF0000) == 0) {
        uint16_t inst16 = (uint16_t)inst;
        
        // MOV Rd, #imm (T1)
        if ((inst16 & 0xF800) == 0x2000) {
            uint8_t rd = (inst16 >> 8) & 0x7;
            uint8_t imm = inst16 & 0xFF;
            core->state.registers[rd] = imm;
            core->state.registers[REG_PC] += 2;
        }
        // ADD Rd, Rn, #imm (T1)
        else if ((inst16 & 0xF800) == 0x3000) {
            uint8_t rd = (inst16 >> 8) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t imm = inst16 & 0xFF;
            core->state.registers[rd] = core_add_with_carry(core,
                core->state.registers[rn], imm, false, true);
            core->state.registers[REG_PC] += 2;
        }
        // SUB Rd, Rn, #imm (T1)
        else if ((inst16 & 0xF800) == 0x3800) {
            uint8_t rd = (inst16 >> 8) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t imm = inst16 & 0xFF;
            core->state.registers[rd] = core_sub_with_borrow(core,
                core->state.registers[rn], imm, false, true);
            core->state.registers[REG_PC] += 2;
        }
        // CMP Rn, #imm (T1)
        else if ((inst16 & 0xF800) == 0x2800) {
            uint8_t rn = (inst16 >> 8) & 0x7;
            uint8_t imm = inst16 & 0xFF;
            core_sub_with_borrow(core,
                core->state.registers[rn], imm, false, true);
            core->state.registers[REG_PC] += 2;
        }
        // B cond (T1)
        else if ((inst16 & 0xF000) == 0xD000) {
            uint8_t cond = (inst16 >> 8) & 0xF;
            int8_t imm = (int8_t)(inst16 & 0xFF);
            int32_t offset = (int32_t)imm * 2;
            
            bool should_branch = false;
            bool n, z, c, v;
            core_get_flags(core, &n, &z, &c, &v);
            
            switch (cond) {
                case 0x0: should_branch = z; break;         // EQ
                case 0x1: should_branch = !z; break;        // NE
                case 0x2: should_branch = c; break;         // CS
                case 0x3: should_branch = !c; break;        // CC
                case 0x4: should_branch = n; break;         // MI
                case 0x5: should_branch = !n; break;        // PL
                case 0x6: should_branch = v; break;         // VS
                case 0x7: should_branch = !v; break;        // VC
                case 0x8: should_branch = c && !z; break;  // HI
                case 0x9: should_branch = !(c && !z); break; // LS
                case 0xA: should_branch = n == v; break;   // GE
                case 0xB: should_branch = n != v; break;    // LT
                case 0xC: should_branch = !z && (n == v); break; // GT
                case 0xD: should_branch = z || (n != v); break;  // LE
                case 0xE: should_branch = true; break;     // AL
                case 0xF: should_branch = false; break;    // NV (undefined)
            }
            
            if (should_branch) {
                core->state.registers[REG_PC] += offset;
                cycles = 2;
            } else {
                core->state.registers[REG_PC] += 2;
            }
        }
        // ADD Rd, Rn, Rm (T1)
        else if ((inst16 & 0xFE00) == 0x1800) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t rm = (inst16 >> 6) & 0x7;
            char log_msg[64];
            snprintf(log_msg, sizeof(log_msg), "ADD R%d, R%d, R%d (Rn=%lX, Rm=%lX)",
                     rd, rn, rm, (unsigned long)core->state.registers[rn],
                     (unsigned long)core->state.registers[rm]);
            core_log(core, log_msg);
            core->state.registers[rd] = core_add_with_carry(core,
                core->state.registers[rn], core->state.registers[rm], false, true);
            core->state.registers[REG_PC] += 2;
        }
        // SUB Rd, Rn, Rm (T1)
        else if ((inst16 & 0xFE00) == 0x1A00) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t rm = (inst16 >> 6) & 0x7;
            char log_msg[64];
            snprintf(log_msg, sizeof(log_msg), "SUB R%d, R%d, R%d (Rn=%lX, Rm=%lX)",
                     rd, rn, rm, (unsigned long)core->state.registers[rn],
                     (unsigned long)core->state.registers[rm]);
            core_log(core, log_msg);
            core->state.registers[rd] = core_sub_with_borrow(core,
                core->state.registers[rn], core->state.registers[rm], false, true);
            core->state.registers[REG_PC] += 2;
        }
        // CMP Rn, Rm (T1)
        else if ((inst16 & 0xFE00) == 0x4200) {
            uint8_t rn = (inst16 >> 0) & 0x7;
            uint8_t rm = (inst16 >> 3) & 0x7;
            core_sub_with_borrow(core,
                core->state.registers[rn], core->state.registers[rm], false, true);
            core->state.registers[REG_PC] += 2;
        }
        // AND Rd, Rn (T1)
        else if ((inst16 & 0xFFC0) == 0x4000) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rm = (inst16 >> 3) & 0x7;
            core->state.registers[rd] &= core->state.registers[rm];
            core_update_nzcv_flags(core, core->state.registers[rd], false, false);
            core->state.registers[REG_PC] += 2;
        }
        // EOR Rd, Rn (T1)
        else if ((inst16 & 0xFFC0) == 0x4040) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rm = (inst16 >> 3) & 0x7;
            core->state.registers[rd] ^= core->state.registers[rm];
            core_update_nzcv_flags(core, core->state.registers[rd], false, false);
            core->state.registers[REG_PC] += 2;
        }
        // LSL Rd, Rn (T1)
        else if ((inst16 & 0xFFC0) == 0x4080) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rm = (inst16 >> 3) & 0x7;
            uint8_t shift = core->state.registers[rm] & 0xFF;
            if (shift < 32) {
                uint32_t result = core->state.registers[rd] << shift;
                bool carry = (shift > 0) && ((core->state.registers[rd] >> (32 - shift)) & 1);
                core->state.registers[rd] = result;
                core_update_nzcv_flags(core, result, carry, false);
            } else if (shift == 32) {
                bool carry = (core->state.registers[rd] & 1) != 0;
                core->state.registers[rd] = 0;
                core_update_nzcv_flags(core, 0, carry, false);
            } else {
                core->state.registers[rd] = 0;
                core_update_nzcv_flags(core, 0, false, false);
            }
            core->state.registers[REG_PC] += 2;
        }
        // LSR Rd, Rn (T1)
        else if ((inst16 & 0xFFC0) == 0x40C0) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rm = (inst16 >> 3) & 0x7;
            uint8_t shift = core->state.registers[rm] & 0xFF;
            if (shift < 32) {
                uint32_t result = core->state.registers[rd] >> shift;
                bool carry = (shift > 0) && ((core->state.registers[rd] >> (shift - 1)) & 1);
                core->state.registers[rd] = result;
                core_update_nzcv_flags(core, result, carry, false);
            } else if (shift == 32) {
                bool carry = (core->state.registers[rd] & (1U << 31)) != 0;
                core->state.registers[rd] = 0;
                core_update_nzcv_flags(core, 0, carry, false);
            } else {
                core->state.registers[rd] = 0;
                core_update_nzcv_flags(core, 0, false, false);
            }
            core->state.registers[REG_PC] += 2;
        }
        // ASR Rd, Rn (T1)
        else if ((inst16 & 0xFFC0) == 0x4100) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rm = (inst16 >> 3) & 0x7;
            uint8_t shift = core->state.registers[rm] & 0xFF;
            if (shift < 32) {
                int32_t signed_val = (int32_t)core->state.registers[rd];
                uint32_t result = (uint32_t)(signed_val >> shift);
                bool carry = (shift > 0) && ((core->state.registers[rd] >> (shift - 1)) & 1);
                core->state.registers[rd] = result;
                core_update_nzcv_flags(core, result, carry, false);
            } else {
                bool carry = (core->state.registers[rd] & (1U << 31)) != 0;
                core->state.registers[rd] = carry ? 0xFFFFFFFF : 0;
                core_update_nzcv_flags(core, core->state.registers[rd], carry, false);
            }
            core->state.registers[REG_PC] += 2;
        }
        // ORR Rd, Rn (T1)
        else if ((inst16 & 0xFFC0) == 0x4300) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rm = (inst16 >> 3) & 0x7;
            core->state.registers[rd] |= core->state.registers[rm];
            core_update_nzcv_flags(core, core->state.registers[rd], false, false);
            core->state.registers[REG_PC] += 2;
        }
        // MUL Rd, Rn (T1)
        else if ((inst16 & 0xFFC0) == 0x4340) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rm = (inst16 >> 3) & 0x7;
            uint32_t rn_val = core->state.registers[rd];
            uint32_t rm_val = core->state.registers[rm];
            uint64_t result_64 = (uint64_t)rn_val * rm_val;
            uint32_t result = (uint32_t)result_64;
            core->state.registers[rd] = result;
            
            // Set N and Z flags
            bool n = (result & (1U << 31)) != 0;
            bool z = (result == 0);
            core->state.xpsr &= ~(XPSR_N_MASK | XPSR_Z_MASK);
            if (n) core->state.xpsr |= XPSR_N_MASK;
            if (z) core->state.xpsr |= XPSR_Z_MASK;
            
            core->state.registers[REG_PC] += 2;
            cycles = 1;  // MUL takes 1 cycle in Cortex-M3
        }
        // B cond (T1)
        else if ((inst16 & 0xF000) == 0xD000) {
            uint8_t cond = (inst16 >> 8) & 0xF;
            int8_t imm = (int8_t)(inst16 & 0xFF);
            int32_t offset = (int32_t)imm * 2;
            
            bool should_branch = false;
            bool n, z, c, v;
            core_get_flags(core, &n, &z, &c, &v);
            
            switch (cond) {
                case 0x0: should_branch = z; break;         // EQ
                case 0x1: should_branch = !z; break;        // NE
                case 0x2: should_branch = c; break;         // CS
                case 0x3: should_branch = !c; break;        // CC
                case 0x4: should_branch = n; break;         // MI
                case 0x5: should_branch = !n; break;        // PL
                case 0x6: should_branch = v; break;         // VS
                case 0x7: should_branch = !v; break;        // VC
                case 0x8: should_branch = c && !z; break;  // HI
                case 0x9: should_branch = !(c && !z); break; // LS
                case 0xA: should_branch = n == v; break;   // GE
                case 0xB: should_branch = n != v; break;    // LT
                case 0xC: should_branch = !z && (n == v); break; // GT
                case 0xD: should_branch = z || (n != v); break;  // LE
                case 0xE: should_branch = true; break;     // AL
                case 0xF: should_branch = false; break;    // NV (undefined)
            }
            
            if (should_branch) {
                core->state.registers[REG_PC] += offset;
                cycles = 2;  // Branch taken takes 2 cycles
            } else {
                core->state.registers[REG_PC] += 2;
            }
        }
        // BX (T1)
        else if ((inst16 & 0xFF80) == 0x4700) {
            uint8_t rm = (inst16 >> 3) & 0xF;
            uint32_t target = core->state.registers[rm];
            
            // Check Thumb bit
            if (target & 1) {
                core->state.is_thumb_mode = true;
                core->state.registers[REG_PC] = target & ~1;
            } else {
                // Switching to ARM mode is not supported in Cortex-M3
                core_log(core, "Error: ARM mode not supported");
                return 1;
            }
            cycles = 3;  // BX takes 3 cycles
        }
        // PUSH (T1)
        else if ((inst16 & 0xFE00) == 0xB400) {
            uint16_t register_list = inst16 & 0xFF;
            bool lr_push = (inst16 & 0x100) != 0;
            
            if (lr_push) {
                register_list |= (1 << REG_LR);
            }
            
            core_push(core, register_list);
            core->state.registers[REG_PC] += 2;
            cycles = 1 + __builtin_popcount(register_list);
        }
        // POP (T1)
        else if ((inst16 & 0xFE00) == 0xBC00) {
            uint16_t register_list = inst16 & 0xFF;
            bool pc_pop = (inst16 & 0x100) != 0;
            
            if (pc_pop) {
                register_list |= (1 << REG_PC);
            }
            
            core_pop(core, register_list);
            
            // If PC was popped, don't increment PC (it's already set)
            if (!pc_pop) {
                core->state.registers[REG_PC] += 2;
            }
            cycles = 1 + __builtin_popcount(register_list);
        }
        // LDR Rd, [PC, #imm] (T1)
        else if ((inst16 & 0xF800) == 0x4800) {
            uint8_t rd = (inst16 >> 8) & 0x7;
            uint8_t imm = (inst16 & 0xFF) * 4;
            uint32_t addr = (core->state.registers[REG_PC] & ~3) + imm;
            core->state.registers[rd] = core_read_word(core, addr);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // LDR Rd, [Rn, #imm] (T1)
        else if ((inst16 & 0xF800) == 0x6800) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t imm = ((inst16 >> 6) & 0x1F) * 4;
            uint32_t addr = core->state.registers[rn] + imm;
            core->state.registers[rd] = core_read_word(core, addr);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // STR Rd, [Rn, #imm] (T1)
        else if ((inst16 & 0xF800) == 0x6000) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t imm = ((inst16 >> 6) & 0x1F) * 4;
            uint32_t addr = core->state.registers[rn] + imm;
            core_write_word(core, addr, core->state.registers[rd]);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // LDRB Rd, [Rn, #imm] (T1)
        else if ((inst16 & 0xF800) == 0x7800) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t imm = (inst16 >> 6) & 0x1F;
            uint32_t addr = core->state.registers[rn] + imm;
            core->state.registers[rd] = core_read_byte(core, addr);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // STRB Rd, [Rn, #imm] (T1)
        else if ((inst16 & 0xF800) == 0x7000) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t imm = (inst16 >> 6) & 0x1F;
            uint32_t addr = core->state.registers[rn] + imm;
            core_write_byte(core, addr, (uint8_t)core->state.registers[rd]);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // LDRH Rd, [Rn, #imm] (T1)
        else if ((inst16 & 0xF800) == 0x8800) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t imm = ((inst16 >> 6) & 0x1F) * 2;
            uint32_t addr = core->state.registers[rn] + imm;
            core->state.registers[rd] = core_read_halfword(core, addr);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // STRH Rd, [Rn, #imm] (T1)
        else if ((inst16 & 0xF800) == 0x8000) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t imm = ((inst16 >> 6) & 0x1F) * 2;
            uint32_t addr = core->state.registers[rn] + imm;
            core_write_halfword(core, addr, (uint16_t)core->state.registers[rd]);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // LDR Rd, [Rn, Rm] (T1)
        else if ((inst16 & 0xFE00) == 0x5800) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t rm = (inst16 >> 6) & 0x7;
            uint32_t addr = core->state.registers[rn] + core->state.registers[rm];
            core->state.registers[rd] = core_read_word(core, addr);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // STR Rd, [Rn, Rm] (T1)
        else if ((inst16 & 0xFE00) == 0x5000) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t rm = (inst16 >> 6) & 0x7;
            uint32_t addr = core->state.registers[rn] + core->state.registers[rm];
            core_write_word(core, addr, core->state.registers[rd]);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // LDRB Rd, [Rn, Rm] (T1)
        else if ((inst16 & 0xFE00) == 0x5C00) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t rm = (inst16 >> 6) & 0x7;
            uint32_t addr = core->state.registers[rn] + core->state.registers[rm];
            core->state.registers[rd] = core_read_byte(core, addr);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // STRB Rd, [Rn, Rm] (T1)
        else if ((inst16 & 0xFE00) == 0x5400) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t rm = (inst16 >> 6) & 0x7;
            uint32_t addr = core->state.registers[rn] + core->state.registers[rm];
            core_write_byte(core, addr, (uint8_t)core->state.registers[rd]);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // LDRH Rd, [Rn, Rm] (T1)
        else if ((inst16 & 0xFE00) == 0x5A00) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t rm = (inst16 >> 6) & 0x7;
            uint32_t addr = core->state.registers[rn] + core->state.registers[rm];
            core->state.registers[rd] = core_read_halfword(core, addr);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // STRH Rd, [Rn, Rm] (T1)
        else if ((inst16 & 0xFE00) == 0x5200) {
            uint8_t rd = (inst16 >> 0) & 0x7;
            uint8_t rn = (inst16 >> 3) & 0x7;
            uint8_t rm = (inst16 >> 6) & 0x7;
            uint32_t addr = core->state.registers[rn] + core->state.registers[rm];
            core_write_halfword(core, addr, (uint16_t)core->state.registers[rd]);
            core->state.registers[REG_PC] += 2;
            cycles = 2;
        }
        // Unknown instruction
        else {
            char log_msg[64];
            snprintf(log_msg, sizeof(log_msg), "Unknown 16-bit %04X", inst16);
            core_log(core, log_msg);
            core->state.registers[REG_PC] += 2;
        }
    } else {
        // 32-bit instructions
        uint16_t inst_hi = (uint16_t)(inst >> 16);
        uint16_t inst_lo = (uint16_t)inst;
        
        // BL (T1)
        if ((inst_hi & 0xF800) == 0xF000 && (inst_lo & 0xD000) == 0xD000) {
            int32_t imm11 = inst_lo & 0x7FF;
            int32_t imm10 = inst_hi & 0x3FF;
            int32_t j1 = (inst_lo >> 13) & 1;
            int32_t j2 = (inst_lo >> 11) & 1;
            int32_t s = (inst_hi >> 10) & 1;
            
            int32_t i1 = ~(j1 ^ s) & 1;
            int32_t i2 = ~(j2 ^ s) & 1;
            
            int32_t offset = (s << 24) | (i1 << 23) | (i2 << 22) | 
                            (imm10 << 12) | (imm11 << 1);
            offset = (offset << 8) >> 8;  // Sign extend
            
            // Save return address
            core->state.registers[REG_LR] = core->state.registers[REG_PC] + 4;
            
            // Branch
            core->state.registers[REG_PC] += offset;
            cycles = 3;
        }
        // B.W (T2)
        else if ((inst_hi & 0xF800) == 0xF000 && (inst_lo & 0xD000) == 0x9000) {
            int32_t imm11 = inst_lo & 0x7FF;
            int32_t imm10 = inst_hi & 0x3FF;
            int32_t j1 = (inst_lo >> 13) & 1;
            int32_t j2 = (inst_lo >> 11) & 1;
            int32_t s = (inst_hi >> 10) & 1;
            
            int32_t i1 = ~(j1 ^ s) & 1;
            int32_t i2 = ~(j2 ^ s) & 1;
            
            int32_t offset = (s << 24) | (i1 << 23) | (i2 << 22) | 
                            (imm10 << 12) | (imm11 << 1);
            offset = (offset << 8) >> 8;  // Sign extend
            
            core->state.registers[REG_PC] += offset;
            cycles = 3;
        }
        // SVC (T1)
        else if ((inst_hi & 0xFFE0) == 0xDF00) {
            uint8_t imm8 = inst_lo & 0xFF;
            char log_msg[64];
            snprintf(log_msg, sizeof(log_msg), "SVC #%d", imm8);
            core_log(core, log_msg);
            core->state.registers[REG_PC] += 4;
            cycles = 3;
        }
        // UDF (T1)
        else if ((inst_hi & 0xFFE0) == 0xDE00) {
            char log_msg[64];
            snprintf(log_msg, sizeof(log_msg), "UDF %08lX", (unsigned long)inst);
            core_log(core, log_msg);
            core->state.registers[REG_PC] += 4;
        }
        else {
            char log_msg[64];
            snprintf(log_msg, sizeof(log_msg), "Unknown %08lX", (unsigned long)inst);
            core_log(core, log_msg);
            core->state.registers[REG_PC] += 4;
        }
    }
    
    return cycles;
}

/* ========================================================================
 * Memory Access Helper Functions
 * ======================================================================== */

uint32_t core_read_word(Core* core, uint32_t address) {
    if (!core || !core->read_memory) {
        return 0;
    }
    
    return core->read_memory(core->memory_context, address, 4);
}

uint16_t core_read_halfword(Core* core, uint32_t address) {
    if (!core || !core->read_memory) {
        return 0;
    }
    
    return (uint16_t)core->read_memory(core->memory_context, address, 2);
}

uint8_t core_read_byte(Core* core, uint32_t address) {
    if (!core || !core->read_memory) {
        return 0;
    }
    
    return (uint8_t)core->read_memory(core->memory_context, address, 1);
}

bool core_write_word(Core* core, uint32_t address, uint32_t value) {
    if (!core || !core->write_memory) {
        return false;
    }
    
    return core->write_memory(core->memory_context, address, 4, value);
}

bool core_write_halfword(Core* core, uint32_t address, uint16_t value) {
    if (!core || !core->write_memory) {
        return false;
    }
    
    return core->write_memory(core->memory_context, address, 2, value);
}

bool core_write_byte(Core* core, uint32_t address, uint8_t value) {
    if (!core || !core->write_memory) {
        return false;
    }
    
    return core->write_memory(core->memory_context, address, 1, value);
}

/* ========================================================================
 * Stack Operations
 * ======================================================================== */

bool core_push(Core* core, uint16_t registers) {
    if (!core) {
        return false;
    }
    
    uint32_t sp = core->state.registers[REG_SP];
    
    // Push registers in order from highest to lowest
    for (int i = 15; i >= 0; i--) {
        if (registers & (1 << i)) {
            sp -= 4;
            core_write_word(core, sp, core->state.registers[i]);
        }
    }
    
    core->state.registers[REG_SP] = sp;
    return true;
}

bool core_pop(Core* core, uint16_t registers) {
    if (!core) {
        return false;
    }
    
    uint32_t sp = core->state.registers[REG_SP];
    
    // Pop registers in order from lowest to highest
    for (int i = 0; i < 16; i++) {
        if (registers & (1 << i)) {
            core->state.registers[i] = core_read_word(core, sp);
            sp += 4;
        }
    }
    
    core->state.registers[REG_SP] = sp;
    return true;
}

bool core_push_value(Core* core, uint32_t value) {
    if (!core) {
        return false;
    }
    
    uint32_t sp = core->state.registers[REG_SP];
    sp -= 4;
    core_write_word(core, sp, value);
    core->state.registers[REG_SP] = sp;
    
    return true;
}

uint32_t core_pop_value(Core* core) {
    if (!core) {
        return 0;
    }
    
    uint32_t sp = core->state.registers[REG_SP];
    uint32_t value = core_read_word(core, sp);
    sp += 4;
    core->state.registers[REG_SP] = sp;
    
    return value;
}

/* ========================================================================
 * Context Save/Restore for Interrupts
 * ======================================================================== */

bool core_save_context(Core* core) {
    if (!core) {
        return false;
    }
    
    // Save xPSR, PC, LR, R12, R3, R2, R1, R0 to stack
    // Order: xPSR, PC, LR, R12, R3, R2, R1, R0
    
    uint32_t sp = core->state.registers[REG_SP];
    
    sp -= 4;
    core_write_word(core, sp, core->state.xpsr);
    
    sp -= 4;
    core_write_word(core, sp, core->state.registers[REG_PC]);
    
    sp -= 4;
    core_write_word(core, sp, core->state.registers[REG_LR]);
    
    sp -= 4;
    core_write_word(core, sp, core->state.registers[REG_R12]);
    
    sp -= 4;
    core_write_word(core, sp, core->state.registers[REG_R3]);
    
    sp -= 4;
    core_write_word(core, sp, core->state.registers[REG_R2]);
    
    sp -= 4;
    core_write_word(core, sp, core->state.registers[REG_R1]);
    
    sp -= 4;
    core_write_word(core, sp, core->state.registers[REG_R0]);
    
    core->state.registers[REG_SP] = sp;
    
    return true;
}

bool core_restore_context(Core* core) {
    if (!core) {
        return false;
    }
    
    // Restore xPSR, PC, LR, R12, R3, R2, R1, R0 from stack
    // Order: xPSR, PC, LR, R12, R3, R2, R1, R0
    
    uint32_t sp = core->state.registers[REG_SP];
    
    core->state.registers[REG_R0] = core_read_word(core, sp);
    sp += 4;
    
    core->state.registers[REG_R1] = core_read_word(core, sp);
    sp += 4;
    
    core->state.registers[REG_R2] = core_read_word(core, sp);
    sp += 4;
    
    core->state.registers[REG_R3] = core_read_word(core, sp);
    sp += 4;
    
    core->state.registers[REG_R12] = core_read_word(core, sp);
    sp += 4;
    
    core->state.registers[REG_LR] = core_read_word(core, sp);
    sp += 4;
    
    core->state.registers[REG_PC] = core_read_word(core, sp);
    sp += 4;
    
    core->state.xpsr = core_read_word(core, sp);
    sp += 4;
    
    core->state.registers[REG_SP] = sp;
    
    return true;
}

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

bool core_is_valid_address(uint32_t address) {
    // Flash memory
    if (address >= FLASH_START && address <= FLASH_END) {
        return true;
    }
    
    // SRAM
    if (address >= SRAM_START && address <= SRAM_END) {
        return true;
    }
    
    // Peripherals
    if (address >= PERIPH_START && address <= PERIPH_END) {
        return true;
    }
    
    return false;
}

uint32_t core_align_word(uint32_t address) {
    return address & ~0x3;
}

bool core_is_word_aligned(uint32_t address) {
    return (address & 0x3) == 0;
}
