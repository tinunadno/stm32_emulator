# Core Module - ARM Cortex-M3 Emulator

## Overview

The Core module emulates an **ARM Cortex-M3** processor (Thumb-2 instruction set) for the STM32F103C8T6 microcontroller. This document explains how the emulator works.

## Main Components

### Core State Structure

```c
typedef struct {
    uint32_t registers[16];  // R0-R15 (SP=R13, LR=R14, PC=R15)
    uint32_t xpsr;           // Processor status register (N,Z,C,V,T flags)
    uint32_t cycles;         // Cycle counter
    uint8_t current_irq;     // Current interrupt number
} CoreState;
```

**Register File:**
- **R0-R12**: General-purpose registers
- **R13 (SP)**: Stack Pointer
- **R14 (LR)**: Link Register (return address)
- **R15 (PC)**: Program Counter

**xPSR Flags:**
- **N**: Negative (result < 0)
- **Z**: Zero (result == 0)
- **C**: Carry (unsigned overflow)
- **V**: Overflow (signed overflow)
- **T**: Thumb mode (always 1 for Cortex-M3)

### Memory Interface

The Core uses callback functions for memory access:

```c
uint32_t (*read_memory)(void* context, uint32_t address, uint8_t size);
bool (*write_memory)(void* context, uint32_t address, uint8_t size, uint32_t data);
```

This allows the Core to work with any memory implementation (RAM, Flash, Peripherals).

## Instruction Execution Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    core_step(core)                          │
│                     (Single step)                           │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│         core_fetch_instruction(core, address)               │
│  • Read 16-bit instruction                                  │
│  • If it's a 32-bit Thumb-2 instruction, read next 16 bits  │
│  • Return 32-bit instruction value                          │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│           core_decode_and_execute(core, inst)               │
│  • Check instruction format (16-bit or 32-bit)             │
│  • Match against instruction patterns                       │
│  • Execute corresponding operation                          │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│              Update xPSR flags (N,Z,C,V)                     │
│  • Arithmetic operations update flags                      │
│  • Compare operations set flags without storing result       │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│              Advance PC (Program Counter)                   │
│  • +2 for 16-bit instructions                               │
│  • +4 for 32-bit instructions                              │
└─────────────────────────────────────────────────────────────┘
```

## Supported Instructions

### Data Processing

| Instruction | Description |
|-------------|-------------|
| `MOV Rd, #imm` | Move immediate value to register |
| `ADD Rd, Rn, Rm` | Add two registers |
| `SUB Rd, Rn, Rm` | Subtract two registers |
| `CMP Rn, Rm` | Compare (subtract without storing result) |
| `AND Rd, Rm` | Bitwise AND |
| `EOR Rd, Rm` | Bitwise XOR |
| `LSL Rd, Rm` | Logical shift left |
| `LSR Rd, Rm` | Logical shift right |
| `ASR Rd, Rm` | Arithmetic shift right |
| `ROR Rd, Rm` | Rotate right |
| `MUL Rd, Rn, Rm` | Multiply |

### Branch Instructions

| Instruction | Description |
|-------------|-------------|
| `B offset` | Unconditional branch |
| `B{cond} offset` | Conditional branch (EQ, NE, LT, GT, LE, GE, CS, CC, MI, PL, VS, VC, HI, LS) |
| `BX Rm` | Branch and exchange (switch to ARM/Thumb mode) |
| `BL offset` | Branch with link (function call) |

### Memory Access

| Instruction | Description |
|-------------|-------------|
| `LDR Rd, [Rn, #imm]` | Load register from memory |
| `STR Rd, [Rn, #imm]` | Store register to memory |
| `LDRH Rd, [Rn, #imm]` | Load halfword (16-bit) |
| `STRH Rd, [Rn, #imm]` | Store halfword (16-bit) |
| `LDRB Rd, [Rn, #imm]` | Load byte (8-bit) |
| `STRB Rd, [Rn, #imm]` | Store byte (8-bit) |

### Stack Operations

| Instruction | Description |
|-------------|-------------|
| `PUSH {Rlist}` | Push registers to stack (decrements SP) |
| `POP {Rlist}` | Pop registers from stack (increments SP) |

### System Instructions

| Instruction | Description |
|-------------|-------------|
| `SVC #imm` | Supervisor call (software interrupt) |
| `UDF #imm` | Undefined instruction (used for breakpoints) |

## Interrupt Handling

### Interrupt Entry

```
┌─────────────────────────────────────────────────────────────┐
│              core_enter_interrupt(core, irq_num)            │
│  1. Save context to stack (R0-R3, R12, LR, PC, xPSR)       │
│  2. Set LR to 0xFFFFFFF9 (exception return)                │
│  3. Set PC to interrupt handler address                     │
│  4. Set current_irq field                                   │
└─────────────────────────────────────────────────────────────┘
```

### Interrupt Exit

```
┌─────────────────────────────────────────────────────────────┐
│              core_exit_interrupt(core)                      │
│  1. Restore context from stack                              │
│  2. Clear current_irq field                                 │
└─────────────────────────────────────────────────────────────┘
```

### Context Frame Layout (on Stack)

```
Stack (grows downward):
┌───────────┐
│   xPSR    │  ← SP after save
├───────────┤
│    PC     │
├───────────┤
│    LR     │
├───────────┤
│   R12     │
├───────────┤
│    R3     │
├───────────┤
│    R2     │
├───────────┤
│    R1     │
├───────────┤
│    R0     │
└───────────┘
```

## Memory Map

| Region | Start Address | End Address | Size | Description |
|--------|---------------|-------------|------|-------------|
| Flash | 0x08000000 | 0x0800FFFF | 64KB | Program memory (read-only) |
| SRAM | 0x20000000 | 0x20004FFF | 20KB | Data memory (read/write) |
| Peripherals | 0x40000000 | 0x5FFFFFFF | - | GPIO, UART, Timers, etc. |

## Example Program

The test program in [`examples/core_example.c`](../examples/core_example.c) demonstrates:

### 1. Register Operations
```c
core_set_register(core, REG_R0, 0x12345678);
core_set_register(core, REG_R1, 0x42);
uint32_t r0 = core_get_register(core, REG_R0);
```

### 2. Memory Operations
```c
core_write_word(core, 0x20000000, 0xDEADBEEF);
core_write_halfword(core, 0x20000004, 0xCAFE);
core_write_byte(core, 0x20000006, 0xBA);
```

### 3. Stack Operations
```c
core_push(core, REG_R0);   // Push R0 to stack
core_push(core, REG_R1);
core_pop(core, REG_R2);     // Pop to R2
```

### 4. Instruction Execution
```c
uint16_t program[] = {
    0x202A,  // MOV R0, #42      ; R0 = 42
    0x210A,  // MOV R1, #10      ; R1 = 10
    0x1808,  // ADD R0, R1, R0   ; R0 = R1 + R0 = 52
    0x2205,  // MOV R2, #5       ; R2 = 5
    0x1A05,  // SUB R5, R0, R0   ; R5 = 0 (Z=1, C=1)
    0x282F,  // CMP R0, #47      ; Compare R0 with 47
    0xD001,  // BEQ +4           ; Branch if equal (not taken)
    0x2000,  // MOV R0, #0       ; R0 = 0 (executed)
    0xE7FE   // B .              ; Infinite loop
};
```

## Key Design Principles

1. **Callback-based I/O** - Memory access is abstracted through callbacks, allowing flexible memory implementations
2. **State encapsulation** - All processor state is contained in a single `CoreState` structure
3. **Modular design** - Separate functions for fetch, decode, and execute phases
4. **Little-endian** - Matches STM32F103C8T6 architecture (LSB at lowest address)
5. **Thumb-2 only** - Cortex-M3 only executes Thumb instructions (T=1 in xPSR)
6. **Cycle counting** - Tracks execution cycles for timing analysis

## Building and Running

```bash
# Compile
make clean && make

# Run tests
./bin/core_example
```

## API Reference

### Initialization
- `core_create()` - Create a new Core instance
- `core_destroy(core)` - Destroy a Core instance
- `core_reset(core)` - Reset processor state

### Execution
- `core_step(core)` - Execute one instruction
- `core_run(core, max_cycles)` - Run until max_cycles or breakpoint

### Register Access
- `core_get_register(core, reg_num)` - Read a register
- `core_set_register(core, reg_num, value)` - Write a register
- `core_get_pc(core)` - Get Program Counter
- `core_get_sp(core)` - Get Stack Pointer
- `core_get_lr(core)` - Get Link Register

### xPSR Access
- `core_get_xpsr(core)` - Read xPSR
- `core_set_xpsr(core, value)` - Write xPSR
- `core_get_flags(core, n, z, c, v)` - Read individual flags
- `core_set_flags(core, n, z, c, v)` - Set individual flags

### Interrupt Handling
- `core_enter_interrupt(core, irq_num)` - Enter interrupt handler
- `core_exit_interrupt(core)` - Exit interrupt handler
- `core_get_current_irq(core)` - Get current interrupt number

### Memory Access
- `core_read_word(core, address)` - Read 32-bit value
- `core_read_halfword(core, address)` - Read 16-bit value
- `core_read_byte(core, address)` - Read 8-bit value
- `core_write_word(core, address, value)` - Write 32-bit value
- `core_write_halfword(core, address, value)` - Write 16-bit value
- `core_write_byte(core, address, value)` - Write 8-bit value

### Stack Operations
- `core_push(core, reg_num)` - Push register to stack
- `core_pop(core, reg_num)` - Pop register from stack
- `core_push_multiple(core, reg_mask)` - Push multiple registers
- `core_pop_multiple(core, reg_mask)` - Pop multiple registers

### Context Management
- `core_save_context(core)` - Save context to stack
- `core_restore_context(core)` - Restore context from stack
