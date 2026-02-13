/**
 * @file core.h
 * @brief ARM Cortex-M3 Core Emulator Module
 * 
 * This module emulates the ARM Cortex-M3 processor core including:
 * - Instruction execution
 * - Register management (R0-R15, xPSR)
 * - Exception and interrupt handling
 */

#ifndef CORE_H
#define CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ========================================================================
 * Constants and Definitions
 * ======================================================================== */

#define NUM_REGISTERS          16
#define XPSR_N_MASK            (1U << 31)  // Negative flag
#define XPSR_Z_MASK            (1U << 30)  // Zero flag
#define XPSR_C_MASK            (1U << 29)  // Carry flag
#define XPSR_V_MASK            (1U << 28)  // Overflow flag
#define XPSR_T_MASK            (1U << 24)  // Thumb state bit (always 1 for Cortex-M3)
#define EXC_RETURN_HANDLER     0xFFFFFFF9  // Return to handler mode
#define EXC_RETURN_THREAD      0xFFFFFFF1  // Return to thread mode

/* Register indices */
#define REG_R0                 0
#define REG_R1                 1
#define REG_R2                 2
#define REG_R3                 3
#define REG_R4                 4
#define REG_R5                 5
#define REG_R6                 6
#define REG_R7                 7
#define REG_R8                 8
#define REG_R9                 9
#define REG_R10                10
#define REG_R11                11
#define REG_R12                12
#define REG_SP                 13  // Stack Pointer
#define REG_LR                 14  // Link Register
#define REG_PC                 15  // Program Counter

/* ========================================================================
 * Data Structures
 * ======================================================================== */

/**
 * @brief Core state structure
 * 
 * Represents the complete state of the ARM Cortex-M3 core
 */
typedef struct {
    uint32_t registers[NUM_REGISTERS];  // R0-R15 (R13=SP, R14=LR, R15=PC)
    uint32_t xpsr;                       // Program Status Register
    bool is_thumb_mode;                  // Execution mode (always true for Cortex-M3)
    bool interruptible;                  // Interrupts enabled flag
    uint32_t current_irq;                // Currently handling interrupt (0 = none)
    uint32_t cycles_executed;            // Cycle counter
    bool is_running;                     // Core running state
    bool is_halted;                      // Core halted state
} CoreState;

/**
 * @brief Instruction decode result
 */
typedef struct {
    uint32_t opcode;           // Full instruction word
    uint32_t address;          // Instruction address
    uint8_t type;              // Instruction type
    uint8_t size;              // Instruction size in bytes
    bool is_valid;             // Valid decode flag
} Instruction;

/**
 * @brief Memory access request
 */
typedef struct {
    uint32_t address;          // Target address
    uint32_t data;             // Data to write (for write operations)
    uint8_t size;              // Access size: 1=byte, 2=halfword, 4=word
    bool is_write;             // true=write, false=read
    uint32_t read_data;        // Data read (for read operations)
    bool success;              // Operation success flag
} MemoryRequest;

/**
 * @brief Interrupt information
 */
typedef struct {
    bool pending;              // Interrupt pending flag
    uint32_t irq_number;       // Interrupt number
    uint32_t vector_address;   // Interrupt vector address
} InterruptInfo;

/* ========================================================================
 * Forward Declarations for Dependencies
 * ======================================================================== */

typedef struct NVIC NVIC;
typedef struct BusController BusController;

/**
 * @brief Core module structure
 * 
 * Contains the core state and references to dependent modules
 */
typedef struct {
    CoreState state;                    // Core state
    NVIC* nvic;                          // NVIC reference
    BusController* bus;                  // Bus controller reference
    
    // Callbacks for memory access
    uint32_t (*read_memory)(void* context, uint32_t address, uint8_t size);
    bool (*write_memory)(void* context, uint32_t address, uint8_t size, uint32_t data);
    void* memory_context;               // Context for memory callbacks
    
    // Logging callback
    void (*log_callback)(const char* message);
} Core;

/* ========================================================================
 * Core API Functions
 * ======================================================================== */

/**
 * @brief Initialize the Core module
 * 
 * @param core Pointer to Core structure
 * @return true if initialization successful, false otherwise
 */
bool core_init(Core* core);

/**
 * @brief Reset the core to initial state
 * 
 * @param core Pointer to Core structure
 */
void core_reset(Core* core);

/**
 * @brief Execute a single instruction
 * 
 * @param core Pointer to Core structure
 * @return true if execution successful, false on error
 */
bool core_step(Core* core);

/**
 * @brief Execute N instructions
 * 
 * @param core Pointer to Core structure
 * @param count Number of instructions to execute
 * @return Number of instructions actually executed
 */
uint32_t core_step_n(Core* core, uint32_t count);

/**
 * @brief Start continuous execution
 * 
 * @param core Pointer to Core structure
 */
void core_run(Core* core);

/**
 * @brief Stop continuous execution
 * 
 * @param core Pointer to Core structure
 */
void core_stop(Core* core);

/**
 * @brief Halt the core (for debugging)
 * 
 * @param core Pointer to Core structure
 */
void core_halt(Core* core);

/**
 * @brief Resume from halt
 * 
 * @param core Pointer to Core structure
 */
void core_resume(Core* core);

/* ========================================================================
 * Register Access Functions
 * ======================================================================== */

/**
 * @brief Get register value
 * 
 * @param core Pointer to Core structure
 * @param reg_num Register number (0-15)
 * @return Register value
 */
uint32_t core_get_register(Core* core, uint8_t reg_num);

/**
 * @brief Set register value
 * 
 * @param core Pointer to Core structure
 * @param reg_num Register number (0-15)
 * @param value Value to set
 * @return true if successful, false if invalid register
 */
bool core_set_register(Core* core, uint8_t reg_num, uint32_t value);

/**
 * @brief Get xPSR value
 * 
 * @param core Pointer to Core structure
 * @return xPSR register value
 */
uint32_t core_get_xpsr(Core* core);

/**
 * @brief Set xPSR value
 * 
 * @param core Pointer to Core structure
 * @param value xPSR value to set
 */
void core_set_xpsr(Core* core, uint32_t value);

/**
 * @brief Get condition flags from xPSR
 * 
 * @param core Pointer to Core structure
 * @param n Negative flag output
 * @param z Zero flag output
 * @param c Carry flag output
 * @param v Overflow flag output
 */
void core_get_flags(Core* core, bool* n, bool* z, bool* c, bool* v);

/**
 * @brief Set condition flags in xPSR
 * 
 * @param core Pointer to Core structure
 * @param n Negative flag
 * @param z Zero flag
 * @param c Carry flag
 * @param v Overflow flag
 */
void core_set_flags(Core* core, bool n, bool z, bool c, bool v);

/* ========================================================================
 * State Query Functions
 * ======================================================================== */

/**
 * @brief Get current program counter
 * 
 * @param core Pointer to Core structure
 * @return Current PC value
 */
uint32_t core_get_pc(Core* core);

/**
 * @brief Get current stack pointer
 * 
 * @param core Pointer to Core structure
 * @return Current SP value
 */
uint32_t core_get_sp(Core* core);

/**
 * @brief Get cycle count
 * 
 * @param core Pointer to Core structure
 * @return Number of cycles executed
 */
uint32_t core_get_cycle_count(Core* core);

/**
 * @brief Get full core state
 * 
 * @param core Pointer to Core structure
 * @return Pointer to CoreState structure
 */
CoreState* core_get_state(Core* core);

/* ========================================================================
 * Interrupt Handling Functions
 * ======================================================================== */

/**
 * @brief Check for pending interrupts
 * 
 * @param core Pointer to Core structure
 * @return true if interrupt pending, false otherwise
 */
bool core_check_interrupt(Core* core);

/**
 * @brief Enter interrupt handler
 * 
 * @param core Pointer to Core structure
 * @param irq_number Interrupt number
 * @param vector_address Interrupt vector address
 */
void core_enter_interrupt(Core* core, uint32_t irq_number, uint32_t vector_address);

/**
 * @brief Exit interrupt handler
 * 
 * @param core Pointer to Core structure
 */
void core_exit_interrupt(Core* core);

/**
 * @brief Enable interrupts
 * 
 * @param core Pointer to Core structure
 */
void core_enable_interrupts(Core* core);

/**
 * @brief Disable interrupts
 * 
 * @param core Pointer to Core structure
 */
void core_disable_interrupts(Core* core);

/* ========================================================================
 * Instruction Execution Functions
 * ======================================================================== */

/**
 * @brief Fetch instruction from memory
 * 
 * @param core Pointer to Core structure
 * @param address Instruction address
 * @return Instruction word
 */
uint32_t core_fetch_instruction(Core* core, uint32_t address);

/**
 * @brief Decode instruction
 * 
 * @param instruction Instruction word
 * @return Instruction type code
 */
uint8_t core_decode_instruction(uint32_t instruction);

/**
 * @brief Execute decoded instruction
 * 
 * @param core Pointer to Core structure
 * @param instruction Instruction structure
 * @return Number of cycles taken
 */
uint32_t core_execute_instruction(Core* core, Instruction* instruction);

/* ========================================================================
 * Memory Access Helper Functions
 * ======================================================================== */

/**
 * @brief Read word from memory
 * 
 * @param core Pointer to Core structure
 * @param address Memory address
 * @return Word value
 */
uint32_t core_read_word(Core* core, uint32_t address);

/**
 * @brief Read halfword from memory
 * 
 * @param core Pointer to Core structure
 * @param address Memory address
 * @return Halfword value
 */
uint16_t core_read_halfword(Core* core, uint32_t address);

/**
 * @brief Read byte from memory
 * 
 * @param core Pointer to Core structure
 * @param address Memory address
 * @return Byte value
 */
uint8_t core_read_byte(Core* core, uint32_t address);

/**
 * @brief Write word to memory
 * 
 * @param core Pointer to Core structure
 * @param address Memory address
 * @param value Word value to write
 * @return true if successful
 */
bool core_write_word(Core* core, uint32_t address, uint32_t value);

/**
 * @brief Write halfword to memory
 * 
 * @param core Pointer to Core structure
 * @param address Memory address
 * @param value Halfword value to write
 * @return true if successful
 */
bool core_write_halfword(Core* core, uint32_t address, uint16_t value);

/**
 * @brief Write byte to memory
 * 
 * @param core Pointer to Core structure
 * @param address Memory address
 * @param value Byte value to write
 * @return true if successful
 */
bool core_write_byte(Core* core, uint32_t address, uint8_t value);

/* ========================================================================
 * Stack Operations
 * ======================================================================== */

/**
 * @brief Push registers to stack
 * 
 * @param core Pointer to Core structure
 * @param registers Bitmask of registers to push
 * @return true if successful
 */
bool core_push(Core* core, uint16_t registers);

/**
 * @brief Pop registers from stack
 * 
 * @param core Pointer to Core structure
 * @param registers Bitmask of registers to pop
 * @return true if successful
 */
bool core_pop(Core* core, uint16_t registers);

/**
 * @brief Push single value to stack
 * 
 * @param core Pointer to Core structure
 * @param value Value to push
 * @return true if successful
 */
bool core_push_value(Core* core, uint32_t value);

/**
 * @brief Pop single value from stack
 * 
 * @param core Pointer to Core structure
 * @return Popped value
 */
uint32_t core_pop_value(Core* core);

/* ========================================================================
 * Context Save/Restore for Interrupts
 * ======================================================================== */

/**
 * @brief Save context on interrupt entry
 * 
 * Saves xPSR, PC, LR, R12, R3, R2, R1, R0 to stack
 * 
 * @param core Pointer to Core structure
 * @return true if successful
 */
bool core_save_context(Core* core);

/**
 * @brief Restore context on interrupt exit
 * 
 * Restores xPSR, PC, LR, R12, R3, R2, R1, R0 from stack
 * 
 * @param core Pointer to Core structure
 * @return true if successful
 */
bool core_restore_context(Core* core);

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

/**
 * @brief Check if address is in valid memory range
 * 
 * @param address Memory address
 * @return true if valid, false otherwise
 */
bool core_is_valid_address(uint32_t address);

/**
 * @brief Align address to word boundary
 * 
 * @param address Memory address
 * @return Aligned address
 */
uint32_t core_align_word(uint32_t address);

/**
 * @brief Check if address is word-aligned
 * 
 * @param address Memory address
 * @return true if aligned, false otherwise
 */
bool core_is_word_aligned(uint32_t address);

#endif /* CORE_H */
