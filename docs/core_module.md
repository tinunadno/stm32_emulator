# ARM Cortex-M3 Core Module

## Overview

The Core module implements the ARM Cortex-M3 processor core emulator for the STM32F103C8T6 microcontroller. This module handles instruction execution, register management, and exception/interrupt handling.

## Architecture

### Core State

The [`CoreState`](../include/core.h:44) structure represents the complete state of the ARM Cortex-M3 core:

```c
typedef struct {
    uint32_t registers[16];  // R0-R15 (R13=SP, R14=LR, R15=PC)
    uint32_t xpsr;           // Program Status Register
    bool is_thumb_mode;      // Execution mode (always true for Cortex-M3)
    bool interruptible;      // Interrupts enabled flag
    uint32_t current_irq;   // Currently handling interrupt (0 = none)
    uint32_t cycles_executed; // Cycle counter
    bool is_running;        // Core running state
    bool is_halted;         // Core halted state
} CoreState;
```

### Register Layout

| Register | Name | Description |
|----------|------|-------------|
| R0-R7    | Low   | General purpose registers |
| R8-R12   | High  | General purpose registers |
| R13      | SP    | Stack Pointer (banked) |
| R14      | LR    | Link Register |
| R15      | PC    | Program Counter |

### xPSR Flags

| Flag | Bit | Description |
|------|-----|-------------|
| N    | 31  | Negative flag |
| Z    | 30  | Zero flag |
| C    | 29  | Carry flag |
| V    | 28  | Overflow flag |
| T    | 24  | Thumb state (always 1) |

## API Reference

### Initialization

#### [`core_init()`](../include/core.h:95)
Initialize the Core module.

```c
bool core_init(Core* core);
```

#### [`core_reset()`](../include/core.h:103)
Reset the core to initial state.

```c
void core_reset(Core* core);
```

### Execution Control

#### [`core_step()`](../include/core.h:111)
Execute a single instruction.

```c
bool core_step(Core* core);
```

#### [`core_step_n()`](../include/core.h:120)
Execute N instructions.

```c
uint32_t core_step_n(Core* core, uint32_t count);
```

#### [`core_run()`](../include/core.h:129)
Start continuous execution.

```c
void core_run(Core* core);
```

#### [`core_stop()`](../include/core.h:137)
Stop continuous execution.

```c
void core_stop(Core* core);
```

### Register Access

#### [`core_get_register()`](../include/core.h:156)
Get register value.

```c
uint32_t core_get_register(Core* core, uint8_t reg_num);
```

#### [`core_set_register()`](../include/core.h:166)
Set register value.

```c
bool core_set_register(Core* core, uint8_t reg_num, uint32_t value);
```

#### [`core_get_xpsr()`](../include/core.h:177)
Get xPSR value.

```c
uint32_t core_get_xpsr(Core* core);
```

#### [`core_get_flags()`](../include/core.h:197)
Get condition flags.

```c
void core_get_flags(Core* core, bool* n, bool* z, bool* c, bool* v);
```

#### [`core_set_flags()`](../include/core.h:208)
Set condition flags.

```c
void core_set_flags(Core* core, bool n, bool z, bool c, bool v);
```

### Memory Access

#### [`core_read_word()`](../include/core.h:265)
Read 32-bit word from memory.

```c
uint32_t core_read_word(Core* core, uint32_t address);
```

#### [`core_read_halfword()`](../include/core.h:275)
Read 16-bit halfword from memory.

```c
uint16_t core_read_halfword(Core* core, uint32_t address);
```

#### [`core_read_byte()`](../include/core.h:285)
Read 8-bit byte from memory.

```c
uint8_t core_read_byte(Core* core, uint32_t address);
```

#### [`core_write_word()`](../include/core.h:296)
Write 32-bit word to memory.

```c
bool core_write_word(Core* core, uint32_t address, uint32_t value);
```

#### [`core_write_halfword()`](../include/core.h:307)
Write 16-bit halfword to memory.

```c
bool core_write_halfword(Core* core, uint32_t address, uint16_t value);
```

#### [`core_write_byte()`](../include/core.h:318)
Write 8-bit byte to memory.

```c
bool core_write_byte(Core* core, uint32_t address, uint8_t value);
```

### Stack Operations

#### [`core_push()`](../include/core.h:345)
Push multiple registers to stack.

```c
bool core_push(Core* core, uint16_t registers);
```

#### [`core_pop()`](../include/core.h:356)
Pop multiple registers from stack.

```c
bool core_pop(Core* core, uint16_t registers);
```

#### [`core_push_value()`](../include/core.h:366)
Push single value to stack.

```c
bool core_push_value(Core* core, uint32_t value);
```

#### [`core_pop_value()`](../include/core.h:377)
Pop single value from stack.

```c
uint32_t core_pop_value(Core* core);
```

### Interrupt Handling

#### [`core_save_context()`](../include/core.h:408)
Save context on interrupt entry (xPSR, PC, LR, R12, R3, R2, R1, R0).

```c
bool core_save_context(Core* core);
```

#### [`core_restore_context()`](../include/core.h:421)
Restore context on interrupt exit.

```c
bool core_restore_context(Core* core);
```

#### [`core_enter_interrupt()`](../include/core.h:249)
Enter interrupt handler.

```c
void core_enter_interrupt(Core* core, uint32_t irq_number, uint32_t vector_address);
```

#### [`core_exit_interrupt()`](../include/core.h:259)
Exit interrupt handler.

```c
void core_exit_interrupt(Core* core);
```

## Supported Instructions

### 16-bit Instructions

| Instruction | Description |
|-------------|-------------|
| MOV Rd, #imm | Move immediate to register |
| ADD Rd, Rn, #imm | Add immediate |
| SUB Rd, Rn, #imm | Subtract immediate |
| ADD Rd, Rn, Rm | Add registers |
| SUB Rd, Rn, Rm | Subtract registers |
| CMP Rn, Rm | Compare registers |
| AND Rd, Rn | Bitwise AND |
| EOR Rd, Rn | Bitwise XOR |
| ORR Rd, Rn | Bitwise OR |
| LSL Rd, Rn | Logical shift left |
| LSR Rd, Rn | Logical shift right |
| ASR Rd, Rn | Arithmetic shift right |
| ROR Rd, Rn | Rotate right |
| MUL Rd, Rn | Multiply |
| B cond | Conditional branch |
| BX Rm | Branch and exchange |
| PUSH {reglist} | Push registers |
| POP {reglist} | Pop registers |
| LDR Rd, [addr] | Load word |
| STR Rd, [addr] | Store word |
| LDRB Rd, [addr] | Load byte |
| STRB Rd, [addr] | Store byte |
| LDRH Rd, [addr] | Load halfword |
| STRH Rd, [addr] | Store halfword |

### 32-bit Instructions

| Instruction | Description |
|-------------|-------------|
| BL label | Branch with link |
| B.W label | Wide branch |
| SVC #imm | Supervisor call |
| UDF #imm | Undefined instruction |

## Memory Map

| Region | Start | End | Size | Description |
|--------|-------|-----|------|-------------|
| Flash | 0x08000000 | 0x0800FFFF | 64 KB | Program memory |
| SRAM | 0x20000000 | 0x20004FFF | 20 KB | Data memory |
| Peripherals | 0x40000000 | 0x5FFFFFFF | - | Peripheral registers |

## Usage Example

```c
#include "core.h"
#include <stdio.h>

// Memory read callback
uint32_t memory_read(void* context, uint32_t address, uint8_t size) {
    // Implement memory read logic
    return 0;
}

// Memory write callback
bool memory_write(void* context, uint32_t address, uint8_t size, uint32_t data) {
    // Implement memory write logic
    return true;
}

int main(void) {
    // Initialize core
    Core core;
    core_init(&core);
    
    // Set up memory callbacks
    core.read_memory = memory_read;
    core.write_memory = memory_write;
    
    // Reset core
    core_reset(&core);
    
    // Set stack pointer
    core_set_register(&core, REG_SP, 0x20004000);
    
    // Execute instructions
    for (int i = 0; i < 100; i++) {
        core_step(&core);
    }
    
    // Print registers
    printf("PC: 0x%08lX\n", core_get_pc(&core));
    printf("R0: 0x%08lX\n", core_get_register(&core, REG_R0));
    
    return 0;
}
```

## Building

```bash
# Build the example
make

# Run the example
make run

# Clean build artifacts
make clean
```

## Dependencies

The Core module depends on the following modules (to be implemented):

- **NVIC** - Nested Vectored Interrupt Controller
- **BusController** - Memory and peripheral bus controller

## Notes

- The Cortex-M3 only supports Thumb mode (Thumb-2 instruction set)
- ARM mode is not supported and will cause an error
- Interrupt handling requires integration with the NVIC module
- Memory access is performed through callbacks for flexibility

## Future Enhancements

- Full Thumb-2 instruction set support
- FPU (Floating Point Unit) emulation (for Cortex-M4F)
- Debug interface support
- Performance optimizations
- More comprehensive instruction coverage
