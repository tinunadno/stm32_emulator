CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

CFLAGS  = -mcpu=cortex-m3 -mthumb -nostdlib -ffreestanding -O0 -Wall
LDFLAGS = -T ../link.ld -nostdlib -nostartfiles

all: hello_world.bin

hello_world.elf: ../startup.c main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

hello_world.bin: hello_world.elf
	$(OBJCOPY) -O binary $< $@
	@echo "Built hello_world.bin ($$(wc -c < $@ | tr -d ' ') bytes)"

clean:
	rm -f hello_world.elf hello_world.bin

.PHONY: all clean
