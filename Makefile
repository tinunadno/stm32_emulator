# Makefile for STM32F103C8T6 Simulator - Core Module
# ===============================================

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I./include
LDFLAGS =

# Directories
SRC_DIR = src
INC_DIR = include
EXAMPLES_DIR = examples
BUILD_DIR = build
BIN_DIR = bin

# Source files
CORE_SRC = $(SRC_DIR)/core.c
CORE_OBJ = $(BUILD_DIR)/core.o

# Example files
CORE_EXAMPLE_SRC = $(EXAMPLES_DIR)/core_example.c
CORE_EXAMPLE_OBJ = $(BUILD_DIR)/core_example.o
CORE_EXAMPLE_BIN = $(BIN_DIR)/core_example

# Targets
.PHONY: all clean dirs

all: dirs $(CORE_EXAMPLE_BIN)

# Create build and binary directories
dirs:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BIN_DIR)

# Build core object file
$(CORE_OBJ): $(CORE_SRC) $(INC_DIR)/core.h
	$(CC) $(CFLAGS) -c $< -o $@

# Build example object file
$(CORE_EXAMPLE_OBJ): $(CORE_EXAMPLE_SRC) $(INC_DIR)/core.h
	$(CC) $(CFLAGS) -c $< -o $@

# Build example executable
$(CORE_EXAMPLE_BIN): $(CORE_OBJ) $(CORE_EXAMPLE_OBJ)
	$(CC) $(LDFLAGS) $^ -o $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Run the example
run: $(CORE_EXAMPLE_BIN)
	$(CORE_EXAMPLE_BIN)

# Help target
help:
	@echo "STM32F103C8T6 Simulator - Core Module"
	@echo "===================================="
	@echo ""
	@echo "Available targets:"
	@echo "  all     - Build all targets (default)"
	@echo "  clean   - Remove build artifacts"
	@echo "  run     - Build and run the example"
	@echo "  help    - Show this help message"
