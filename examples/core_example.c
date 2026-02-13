/**
 * @file core_example.c
 * @brief Example usage of the ARM Cortex-M3 Core Emulator
 * 
 * This example demonstrates basic usage of the Core module including:
 * - Initialization
 * - Register manipulation
 * - Memory operations
 * - Stack operations
 * - Context save/restore
 * - Instruction execution
 * - Interrupt simulation
 */

#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Simple Memory Implementation for Testing
 * ======================================================================== */

#define MEMORY_SIZE (64 * 1024 + 20 * 1024)  // Flash + SRAM

typedef struct {
    uint8_t data[MEMORY_SIZE];
} SimpleMemory;

static SimpleMemory g_memory;

/* Memory read callback */
uint32_t memory_read(void* context, uint32_t address, uint8_t size) {
    SimpleMemory* mem = (SimpleMemory*)context;
    uint32_t offset;
    
    // Handle different address ranges
    if (address >= 0x08000000 && address < 0x08010000) {
        // Flash memory
        offset = address - 0x08000000;
        if (offset + size > 64 * 1024) {
            return 0;
        }
    } else if (address >= 0x20000000 && address < 0x20005000) {
        // SRAM
        offset = address - 0x20000000;
        if (offset + size > 20 * 1024) {
            return 0;
        }
    } else {
        // Out of range
        return 0;
    }
    
    // Read value (little-endian)
    uint32_t result = 0;
    for (int i = 0; i < size; i++) {
        result |= mem->data[offset + i] << (i * 8);
    }
    
    return result;
}

/* Memory write callback */
bool memory_write(void* context, uint32_t address, uint8_t size, uint32_t data) {
    SimpleMemory* mem = (SimpleMemory*)context;
    
    // Only allow writes to SRAM
    if (address < 0x20000000 || address >= 0x20005000) {
        return false;
    }
    
    uint32_t offset = address - 0x20000000;
    if (offset + size > 20 * 1024) {
        return false;
    }
    
    // Write value (little-endian)
    for (int i = 0; i < size; i++) {
        mem->data[offset + i] = (data >> (i * 8)) & 0xFF;
    }
    
    return true;
}

/* Logging callback */
void log_message(const char* message) {
    printf("[LOG] %s\n", message);
}

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

void print_registers(Core* core) {
    printf("Registers:\n");
    printf("  R0 : 0x%08lX  R1 : 0x%08lX  R2 : 0x%08lX  R3 : 0x%08lX\n",
           (unsigned long)core_get_register(core, 0),
           (unsigned long)core_get_register(core, 1),
           (unsigned long)core_get_register(core, 2),
           (unsigned long)core_get_register(core, 3));
    printf("  R4 : 0x%08lX  R5 : 0x%08lX  R6 : 0x%08lX  R7 : 0x%08lX\n",
           (unsigned long)core_get_register(core, 4),
           (unsigned long)core_get_register(core, 5),
           (unsigned long)core_get_register(core, 6),
           (unsigned long)core_get_register(core, 7));
    printf("  R8 : 0x%08lX  R9 : 0x%08lX  R10: 0x%08lX  R11: 0x%08lX\n",
           (unsigned long)core_get_register(core, 8),
           (unsigned long)core_get_register(core, 9),
           (unsigned long)core_get_register(core, 10),
           (unsigned long)core_get_register(core, 11));
    printf("  R12: 0x%08lX  SP : 0x%08lX  LR : 0x%08lX  PC : 0x%08lX\n",
           (unsigned long)core_get_register(core, 12),
           (unsigned long)core_get_sp(core),
           (unsigned long)core_get_register(core, 14),
           (unsigned long)core_get_pc(core));
    
    // Print flags
    bool n, z, c, v;
    core_get_flags(core, &n, &z, &c, &v);
    printf("  xPSR: 0x%08lX  [N=%d Z=%d C=%d V=%d T=1]\n",
           (unsigned long)core_get_xpsr(core), n, z, c, v);
}

void print_flags(Core* core) {
    bool n, z, c, v;
    core_get_flags(core, &n, &z, &c, &v);
    printf("Flags: N=%d, Z=%d, C=%d, V=%d\n", n, z, c, v);
}

/* ========================================================================
 * Test Functions
 * ======================================================================== */

void test_basic_operations(Core* core) {
    printf("\n=== Test 1: Basic Register Operations ===\n");
    
    // Set some register values
    core_set_register(core, REG_R0, 0x12345678);
    core_set_register(core, REG_R1, 0x00000042);
    core_set_register(core, REG_R2, 0x00000010);
    
    print_registers(core);
    printf("Cycles: %lu\n", (unsigned long)core_get_cycle_count(core));
}

void test_memory_operations(Core* core) {
    printf("\n=== Test 2: Memory Operations ===\n");
    
    // Write to SRAM
    uint32_t sram_addr = 0x20000000;
    core_write_word(core, sram_addr, 0xDEADBEEF);
    core_write_halfword(core, sram_addr + 4, 0xCAFE);
    core_write_byte(core, sram_addr + 6, 0xBA);
    
    // Read back
    printf("Written to memory:\n");
    printf("  0x%08lX: 0x%08lX\n", (unsigned long)sram_addr,
           (unsigned long)core_read_word(core, sram_addr));
    printf("  0x%08lX: 0x%04lX\n", (unsigned long)(sram_addr + 4),
           (unsigned long)core_read_halfword(core, sram_addr + 4));
    printf("  0x%08lX: 0x%02lX\n", (unsigned long)(sram_addr + 6),
           (unsigned long)core_read_byte(core, sram_addr + 6));
}

void test_stack_operations(Core* core) {
    printf("\n=== Test 3: Stack Operations ===\n");
    
    // Initialize SP to a valid SRAM address
    core_set_register(core, REG_SP, 0x20004000);
    
    printf("Initial SP: 0x%08lX\n", (unsigned long)core_get_sp(core));
    
    // Push some values
    core_push_value(core, 0x11111111);
    core_push_value(core, 0x22222222);
    core_push_value(core, 0x33333333);
    
    printf("After pushing 3 values, SP: 0x%08lX\n", (unsigned long)core_get_sp(core));
    
    // Pop values
    uint32_t val1 = core_pop_value(core);
    uint32_t val2 = core_pop_value(core);
    uint32_t val3 = core_pop_value(core);
    
    printf("Popped: 0x%08lX, 0x%08lX, 0x%08lX\n",
           (unsigned long)val1, (unsigned long)val2, (unsigned long)val3);
    printf("After popping 3 values, SP: 0x%08lX\n", (unsigned long)core_get_sp(core));
}

void test_context_save_restore(Core* core) {
    printf("\n=== Test 4: Context Save/Restore ===\n");
    
    // Set up some initial values
    core_set_register(core, REG_R0, 0xAAAA0001);
    core_set_register(core, REG_R1, 0xAAAA0002);
    core_set_register(core, REG_R2, 0xAAAA0003);
    core_set_register(core, REG_R3, 0xAAAA0004);
    core_set_register(core, REG_R12, 0xAAAA0005);
    core_set_register(core, REG_LR, 0xAAAA0006);
    core_set_register(core, REG_PC, 0x08000100);
    core_set_xpsr(core, 0x01000000);
    
    core_set_register(core, REG_SP, 0x20004000);
    
    printf("Before save:\n");
    printf("  R0=%lX R1=%lX R2=%lX R3=%lX R12=%lX LR=%lX PC=%lX xPSR=%lX\n",
           (unsigned long)core_get_register(core, REG_R0),
           (unsigned long)core_get_register(core, REG_R1),
           (unsigned long)core_get_register(core, REG_R2),
           (unsigned long)core_get_register(core, REG_R3),
           (unsigned long)core_get_register(core, REG_R12),
           (unsigned long)core_get_register(core, REG_LR),
           (unsigned long)core_get_pc(core),
           (unsigned long)core_get_xpsr(core));
    printf("  SP: 0x%08lX\n", (unsigned long)core_get_sp(core));
    
    // Save context
    core_save_context(core);
    
    printf("After save, SP: 0x%08lX\n", (unsigned long)core_get_sp(core));
    
    // Modify registers
    core_set_register(core, REG_R0, 0xBBBB0001);
    core_set_register(core, REG_R1, 0xBBBB0002);
    core_set_register(core, REG_R2, 0xBBBB0003);
    core_set_register(core, REG_R3, 0xBBBB0004);
    core_set_register(core, REG_R12, 0xBBBB0005);
    core_set_register(core, REG_LR, 0xBBBB0006);
    core_set_register(core, REG_PC, 0xBBBB0007);
    core_set_xpsr(core, 0x02000000);
    
    printf("After modification:\n");
    printf("  R0=%lX R1=%lX R2=%lX R3=%lX R12=%lX LR=%lX PC=%lX xPSR=%lX\n",
           (unsigned long)core_get_register(core, REG_R0),
           (unsigned long)core_get_register(core, REG_R1),
           (unsigned long)core_get_register(core, REG_R2),
           (unsigned long)core_get_register(core, REG_R3),
           (unsigned long)core_get_register(core, REG_R12),
           (unsigned long)core_get_register(core, REG_LR),
           (unsigned long)core_get_pc(core),
           (unsigned long)core_get_xpsr(core));
    
    // Restore context
    core_restore_context(core);
    
    printf("After restore:\n");
    printf("  R0=%lX R1=%lX R2=%lX R3=%lX R12=%lX LR=%lX PC=%lX xPSR=%lX\n",
           (unsigned long)core_get_register(core, REG_R0),
           (unsigned long)core_get_register(core, REG_R1),
           (unsigned long)core_get_register(core, REG_R2),
           (unsigned long)core_get_register(core, REG_R3),
           (unsigned long)core_get_register(core, REG_R12),
           (unsigned long)core_get_register(core, REG_LR),
           (unsigned long)core_get_pc(core),
           (unsigned long)core_get_xpsr(core));
    printf("  SP: 0x%08lX\n", (unsigned long)core_get_sp(core));
}

void test_instruction_execution(Core* core) {
    printf("\n=== Test 5: Instruction Execution ===\n");
    
    // Reset core
    core_reset(core);
    core_set_register(core, REG_SP, 0x20004000);
    
    // Write program directly to memory array (using SRAM for testing)
    // Address 0x20002000:
    //   MOV R0, #42      (0x202A) - Load 42 into R0
    //   MOV R1, #10      (0x210A) - Load 10 into R1
    //   ADD R1, R0       (0x1808) - R1 = R1 + R0 (52)
    //   MOV R2, #5       (0x2205) - Load 5 into R2
    //   SUB R1, R2       (0x1A05) - R1 = R1 - R2 (47)
    //   CMP R1, #47      (0x282F) - Compare R1 with 47
    //   BEQ +4           (0xD001) - Branch if equal to skip
    //   MOV R0, #0       (0x2000) - Set R0 to 0 (not executed)
    //   B .              (0xE7FE) - Infinite loop
    
    uint32_t program_addr = 0x20002000;
    uint16_t program[] = {
        0x202A,  // MOV R0, #42
        0x210A,  // MOV R1, #10
        0x1808,  // ADD R1, R0
        0x2205,  // MOV R2, #5
        0x1A05,  // SUB R1, R2
        0x282F,  // CMP R1, #47
        0xD001,  // BEQ +4
        0x2000,  // MOV R0, #0 (skipped)
        0xE7FE   // B .
    };
    
    // Write program to SRAM (directly to memory array)
    for (int i = 0; i < 9; i++) {
        uint32_t offset = program_addr - 0x20000000 + i * 2;
        g_memory.data[offset] = program[i] & 0xFF;
        g_memory.data[offset + 1] = (program[i] >> 8) & 0xFF;
    }
    
    // Set PC to program start
    core_set_register(core, REG_PC, program_addr);
    
    printf("Program loaded at 0x%08lX\n", (unsigned long)program_addr);
    
    // Execute instructions step by step
    printf("\nExecuting instructions:\n");
    
    for (int i = 0; i < 8; i++) {
        printf("\nStep %d:\n", i + 1);
        printf("  PC: 0x%08lX\n", (unsigned long)core_get_pc(core));
        
        uint16_t inst = (uint16_t)core_fetch_instruction(core, core_get_pc(core));
        printf("  Instruction: 0x%04X\n", inst);
        
        core_step(core);
        
        printf("  R0: 0x%08lX  R1: 0x%08lX  R2: 0x%08lX\n",
               (unsigned long)core_get_register(core, REG_R0),
               (unsigned long)core_get_register(core, REG_R1),
               (unsigned long)core_get_register(core, REG_R2));
        print_flags(core);
    }
    
    printf("\nFinal state:\n");
    printf("  R0: 0x%08lX (expected: 0x0000002F = 47)\n",
           (unsigned long)core_get_register(core, REG_R0));
    printf("  Cycles: %lu\n", (unsigned long)core_get_cycle_count(core));
}

void test_interrupt_simulation(Core* core) {
    printf("\n=== Test 6: Interrupt Simulation ===\n");
    
    // Reset core
    core_reset(core);
    core_set_register(core, REG_SP, 0x20004000);
    core_set_register(core, REG_R0, 0x11111111);
    core_set_register(core, REG_R1, 0x22222222);
    
    printf("Before interrupt:\n");
    printf("  R0: 0x%08lX  R1: 0x%08lX  PC: 0x%08lX  SP: 0x%08lX\n",
           (unsigned long)core_get_register(core, REG_R0),
           (unsigned long)core_get_register(core, REG_R1),
           (unsigned long)core_get_pc(core),
           (unsigned long)core_get_sp(core));
    
    // Simulate entering an interrupt
    uint32_t irq_number = 11;  // TIM2
    uint32_t vector_address = 0x08000100;
    core_enter_interrupt(core, irq_number, vector_address);
    
    printf("After entering interrupt:\n");
    printf("  R0: 0x%08lX  R1: 0x%08lX  PC: 0x%08lX  SP: 0x%08lX\n",
           (unsigned long)core_get_register(core, REG_R0),
           (unsigned long)core_get_register(core, REG_R1),
           (unsigned long)core_get_pc(core),
           (unsigned long)core_get_sp(core));
    printf("  LR: 0x%08lX (should be 0xFFFFFFF9)\n",
           (unsigned long)core_get_register(core, REG_LR));
    printf("  Current IRQ: %lu\n", (unsigned long)core->state.current_irq);
    
    // Simulate exiting the interrupt
    core_exit_interrupt(core);
    
    printf("After exiting interrupt:\n");
    printf("  R0: 0x%08lX  R1: 0x%08lX  PC: 0x%08lX  SP: 0x%08lX\n",
           (unsigned long)core_get_register(core, REG_R0),
           (unsigned long)core_get_register(core, REG_R1),
           (unsigned long)core_get_pc(core),
           (unsigned long)core_get_sp(core));
    printf("  Current IRQ: %lu\n", (unsigned long)core->state.current_irq);
}

/* ========================================================================
 * Main Function
 * ======================================================================== */

int main(void) {
    printf("STM32F103C8T6 Core Emulator - Example Program\n");
    printf("==============================================\n");
    
    // Initialize memory
    memset(&g_memory, 0, sizeof(g_memory));
    
    // Initialize core
    Core core;
    if (!core_init(&core)) {
        printf("Failed to initialize core!\n");
        return 1;
    }
    
    // Set up memory callbacks
    core.read_memory = memory_read;
    core.write_memory = memory_write;
    core.memory_context = &g_memory;
    core.log_callback = log_message;
    
    // Run tests
    test_basic_operations(&core);
    test_memory_operations(&core);
    test_stack_operations(&core);
    test_context_save_restore(&core);
    test_instruction_execution(&core);
    test_interrupt_simulation(&core);
    
    printf("\n=== All tests completed ===\n");
    
    return 0;
}
