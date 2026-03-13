#!/usr/bin/env python3
"""
Generate a test firmware.bin for the STM32 simulator.

The program:
  1. Prints "Hi!\n" via UART
  2. Starts TIM2 with period 50
  3. Polls timer overflow 3 times, printing "1\n", "2\n", "3\n"
  4. Halts (infinite loop)

Uses a literal pool for 32-bit constants — much more reliable than
trying to build addresses with MOV+LSL+ADD chains.
"""
import struct

# =============================================================================
# Minimal Thumb assembler
# =============================================================================

flash = bytearray(1024)
pc = 0  # current offset in flash


def emit(val):
    global pc
    struct.pack_into('<H', flash, pc, val & 0xFFFF)
    pc += 2


def emit32_data(val):
    global pc
    struct.pack_into('<I', flash, pc, val)
    pc += 4


def pad_to(offset):
    global pc
    while pc < offset:
        emit(0x0000)


# --- Instruction helpers (each emits one 16-bit instruction) ---

def MOV(rd, imm8):
    """MOV Rd, #imm8"""
    emit(0x2000 | (rd << 8) | (imm8 & 0xFF))


def ADD(rd, imm8):
    """ADD Rd, #imm8"""
    emit(0x3000 | (rd << 8) | (imm8 & 0xFF))


def CMP(rn, imm8):
    """CMP Rn, #imm8"""
    emit(0x2800 | (rn << 8) | (imm8 & 0xFF))


def STR(rd, rn, offset_bytes):
    """STR Rd, [Rn, #offset]  (word store, offset must be multiple of 4)"""
    emit(0x6000 | (((offset_bytes >> 2) & 0x1F) << 6) | (rn << 3) | rd)


def LDR(rd, rn, offset_bytes):
    """LDR Rd, [Rn, #offset]  (word load, offset must be multiple of 4)"""
    emit(0x6800 | (((offset_bytes >> 2) & 0x1F) << 6) | (rn << 3) | rd)


def LDR_PC(rd, literal_flash_offset):
    """LDR Rd, [PC, #...] — loads a 32-bit value from the literal pool"""
    aligned_pc4 = (pc + 4) & ~3
    imm8 = (literal_flash_offset - aligned_pc4) >> 2
    assert 0 <= imm8 <= 255, f"Literal pool too far: imm8={imm8}"
    emit(0x4800 | (rd << 8) | imm8)


def TST(rn, rm):
    """TST Rn, Rm"""
    emit(0x4200 | (rm << 3) | rn)


def MOV_REG(rd, rs):
    """MOV Rd, Rs  (via LSL Rd, Rs, #0 for low registers)"""
    emit(0x0000 | (rs << 3) | rd)


def BEQ(target_flash_offset):
    """BEQ target  (backward branch only for simplicity)"""
    offset = target_flash_offset - (pc + 4)
    emit(0xD000 | ((offset >> 1) & 0xFF))


def BNE(target_flash_offset):
    """BNE target"""
    offset = target_flash_offset - (pc + 4)
    emit(0xD100 | ((offset >> 1) & 0xFF))


def B_SELF():
    """B .  (infinite loop)"""
    emit(0xE7FE)


# --- Inline UART putc + TXE poll ---

def uart_putc(char_val):
    """
    Send one character via UART and poll until TXE is set.
    Requires R4 = USART1_BASE.
    Clobbers R0, R1, R2.
    """
    MOV(0, char_val)
    STR(0, 4, 0x04)        # USART1_DR
    poll = pc
    LDR(1, 4, 0x00)        # USART1_SR
    MOV(2, 0x80)            # TXE = bit 7
    TST(1, 2)
    BEQ(poll)


# =============================================================================
# Literal pool addresses (placed after all code at offset 0xC0)
# =============================================================================

LIT_USART1_BASE = 0xC0     # will contain 0x40013800
LIT_TIM2_BASE   = 0xC4     # will contain 0x40000000
LIT_UART_CR1    = 0xC8     # will contain 0x00002008 (UE | TE)

# =============================================================================
# Build firmware
# =============================================================================

# ---- Vector table (offset 0x00) ----
emit32_data(0x20004FF0)     # [0x00] Initial SP (inside SRAM)
emit32_data(0x08000041)     # [0x04] Reset handler → 0x08000040 + Thumb bit

# ---- Pad to code start ----
pad_to(0x40)

# ---- UART init ----
LDR_PC(4, LIT_USART1_BASE) # R4 = USART1_BASE (0x40013800)
LDR_PC(5, LIT_TIM2_BASE)   # R5 = TIM2_BASE   (0x40000000)
LDR_PC(0, LIT_UART_CR1)    # R0 = 0x2008
STR(0, 4, 0x0C)            # USART1_CR1 = UE | TE

# ---- Print "Hi!\n" ----
uart_putc(ord('H'))
uart_putc(ord('i'))
uart_putc(ord('!'))
uart_putc(0x0A)

# ---- Timer init: ARR=50, PSC=0, CEN=1 ----
MOV(0, 50)
STR(0, 5, 0x2C)            # TIM2_ARR = 50
MOV(0, 0)
STR(0, 5, 0x28)            # TIM2_PSC = 0
MOV(0, 1)
STR(0, 5, 0x00)            # TIM2_CR1 = CEN

# ---- Overflow counter ----
MOV(7, 0)                  # R7 = 0

# ---- Timer poll loop ----
timer_poll = pc
LDR(0, 5, 0x10)            # R0 = TIM2_SR
MOV(1, 1)                  # R1 = 1 (UIF mask)
TST(0, 1)
BEQ(timer_poll)            # spin until UIF set

# ---- Overflow! Clear flag, count, print digit ----
MOV(0, 0)
STR(0, 5, 0x10)            # TIM2_SR = 0  (clear UIF)
ADD(7, 1)                  # R7++ (overflows)

MOV_REG(0, 7)              # R0 = R7
ADD(0, ord('0'))            # R0 = '0' + overflows
STR(0, 4, 0x04)            # USART1_DR
poll_digit = pc
LDR(1, 4, 0x00)
MOV(2, 0x80)
TST(1, 2)
BEQ(poll_digit)

MOV(0, 0x0A)               # '\n'
STR(0, 4, 0x04)
poll_nl = pc
LDR(1, 4, 0x00)
MOV(2, 0x80)
TST(1, 2)
BEQ(poll_nl)

# ---- Check if 3 overflows ----
CMP(7, 3)
BNE(timer_poll)            # loop back if < 3

# ---- Halt ----
halt_addr = pc
B_SELF()

# ---- Literal pool ----
pad_to(LIT_USART1_BASE)
emit32_data(0x40013800)     # USART1_BASE
emit32_data(0x40000000)     # TIM2_BASE
emit32_data(0x00002008)     # USART1_CR1 init value (UE | TE)

# =============================================================================
# Write output
# =============================================================================

with open('firmware.bin', 'wb') as f:
    f.write(flash[:pc])

print(f"Generated firmware.bin ({pc} bytes)")
print(f"  Code: 0x40..0x{halt_addr + 1:02X}")
print(f"  Halt: 0x{halt_addr:02X}  (absolute: 0x{0x08000000 + halt_addr:08X})")
print()
print("Usage:")
print(f"  ../src/stm32sim firmware.bin")
print()
print("Then in CLI:")
print(f"  break 0x{0x08000000 + halt_addr:08X}")
print(f"  run")
print()
print("Expected output: Hi!  then  1  2  3  (each on its own line)")
