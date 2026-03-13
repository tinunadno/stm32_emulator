#!/usr/bin/env python3
"""
Generate a minimal empty ARM ELF file.
Used as a placeholder for VS Code cppdbg so it can detect ARM architecture
when debugging via GDB remote without a real firmware.elf.
"""
import struct
import os

def make_arm_elf():
    # ELF identifier (16 bytes)
    e_ident  = b'\x7fELF'   # EI_MAG:   magic number
    e_ident += b'\x01'       # EI_CLASS: ELFCLASS32
    e_ident += b'\x01'       # EI_DATA:  ELFDATA2LSB (little-endian)
    e_ident += b'\x01'       # EI_VERSION: EV_CURRENT
    e_ident += b'\x00'       # EI_OSABI: ELFOSABI_NONE
    e_ident += b'\x00' * 8   # EI_ABIVERSION + padding

    # ELF32 header fields
    header  = e_ident
    header += struct.pack('<H', 2)           # e_type:    ET_EXEC
    header += struct.pack('<H', 0x28)        # e_machine: EM_ARM (40)
    header += struct.pack('<I', 1)           # e_version: EV_CURRENT
    header += struct.pack('<I', 0x08000000)  # e_entry:   Cortex-M flash base
    header += struct.pack('<I', 0)           # e_phoff:   no program headers
    header += struct.pack('<I', 0)           # e_shoff:   no section headers
    header += struct.pack('<I', 0x05000000)  # e_flags:   ARM EABI v5
    header += struct.pack('<H', 52)          # e_ehsize:  ELF header size
    header += struct.pack('<H', 32)          # e_phentsize
    header += struct.pack('<H', 0)           # e_phnum
    header += struct.pack('<H', 40)          # e_shentsize
    header += struct.pack('<H', 0)           # e_shnum
    header += struct.pack('<H', 0)           # e_shstrndx

    return header

if __name__ == '__main__':
    output = os.path.join(os.path.dirname(__file__), 'stub.elf')
    with open(output, 'wb') as f:
        f.write(make_arm_elf())
    print(f'Generated {output} ({len(make_arm_elf())} bytes)')
