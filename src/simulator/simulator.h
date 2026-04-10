#ifndef STM32_SIMULATOR_H
#define STM32_SIMULATOR_H

#include "common/status.h"
#include "memory/memory.h"
#include "nvic/nvic.h"
#include "bus/bus.h"
#include "core/core.h"
#include "debugger/debugger.h"
#include "events/event_queue.h"
#include "peripherals/peripheral.h"
#include "peripherals/timer/timer.h"
#include "peripherals/uart/uart.h"
#include "peripherals/uart/uart_logger.h"
#include "peripherals/systick/systick.h"
#include "peripherals/rcc/rcc.h"
#include "peripherals/gpio/gpio.h"
#include "profiler.h"

#define SIM_MAX_PERIPHERALS 16

/**
 * Main simulator orchestrator.
 * Owns all subsystems and drives the tick-step-check cycle.
 *
 * Adding a new peripheral:
 *   1. Create and init your peripheral
 *   2. Call simulator_add_peripheral() with its Peripheral interface and bus address
 *   3. The simulator will automatically tick, reset, and route bus accesses to it
 */
typedef struct {
    /* Subsystems (owned) */
    Memory      memory;
    NVIC        nvic;
    Bus         bus;
    Core        core;
    Debugger    debugger;
    EventQueue   events;
    TimerState   timer;
    SysTickState systick;
    RccState     rcc;
    GpioState    gpioa;
    GpioState    gpiob;
    GpioState    gpioc;
    UartState    uart;
    UartLogger   uart_logger;

    /* Registered tickable peripherals */
    Peripheral  peripherals[SIM_MAX_PERIPHERALS];
    int         num_peripherals;

    /* Profiler */
    Profiler    profiler;

    /* Simulator state */
    uint64_t    cycle;   /* Total CPU cycles elapsed since reset */
    int         halted;
    int         running;
} Simulator;

void   simulator_init(Simulator* sim);
void   simulator_reset(Simulator* sim);
Status simulator_step(Simulator* sim);
void   simulator_run(Simulator* sim);
void   simulator_halt(Simulator* sim);

/** Load a binary file into flash. */
Status simulator_load(Simulator* sim, const char* path);

/**
 * Register an additional peripheral.
 * bus_base/bus_size define the memory-mapped address range (0 if not bus-mapped).
 */
Status simulator_add_peripheral(Simulator* sim, Peripheral periph,
                                uint32_t bus_base, uint32_t bus_size);

#endif /* STM32_SIMULATOR_H */
