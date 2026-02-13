CC       = gcc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c11 -I.
LDFLAGS  =
TARGET   = stm32sim

# Library sources (everything except main.c)
LIB_SRCS = core/core.c \
           nvic/nvic.c \
           memory/memory.c \
           bus/bus.c \
           peripherals/timer/timer.c \
           peripherals/uart/uart.c \
           debugger/debugger.c \
           simulator/simulator.c \
           ui/ui.c

SRCS     = main.c $(LIB_SRCS)
OBJS     = $(SRCS:.c=.o)
LIB_OBJS = $(LIB_SRCS:.c=.o)

# Test sources
TEST_SRCS = tests/test_main.c \
            tests/test_nvic.c \
            tests/test_memory.c \
            tests/test_bus.c \
            tests/test_timer.c \
            tests/test_uart.c \
            tests/test_core.c \
            tests/test_debugger.c \
            tests/test_integration.c

TEST_OBJS   = $(TEST_SRCS:.c=.o)
TEST_TARGET = test_runner

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(TEST_TARGET): $(TEST_OBJS) $(LIB_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

test: $(TEST_TARGET)
	./$(TEST_TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TARGET) $(TEST_TARGET)
