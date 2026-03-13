CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

CFLAGS  = -mcpu=cortex-m3 -mthumb -nostdlib -ffreestanding -g -O0 -Wall
LDFLAGS = -T link.ld -nostdlib -nostartfiles

all: firmware.bin
	@echo ""
	@echo "=== Build complete ==="
	@echo "Run with:  ../src/stm32sim firmware.bin"

firmware.elf: startup.c main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(SIZE) $@

firmware.bin: firmware.elf
	$(OBJCOPY) -O binary $< $@
	@echo "Generated firmware.bin ($$(wc -c < $@ | tr -d ' ') bytes)"

clean:
	rm -f firmware.elf firmware.bin

.PHONY: all clean
