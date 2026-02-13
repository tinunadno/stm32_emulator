#include "simulator/simulator.h"
#include <stdio.h>
#include <string.h>

/* TIM2 base address and IRQ */
#define TIM2_BASE  0x40000000U
#define TIM2_SIZE  0x400U
#define TIM2_IRQ   28

/* USART1 base address and IRQ */
#define USART1_BASE 0x40013800U
#define USART1_SIZE 0x400U
#define USART1_IRQ  37

/* Default UART output: print to stdout */
static void default_uart_output(char c, void* user_data)
{
    (void)user_data;
    putchar(c);
    fflush(stdout);
}

void simulator_init(Simulator* sim)
{
    memset(sim, 0, sizeof(Simulator));

    /* Initialize subsystems in dependency order (per spec section 18) */
    memory_init(&sim->memory);
    nvic_init(&sim->nvic);
    bus_init(&sim->bus);

    /* Register flash at 0x00000000 (alias) and 0x08000000 */
    bus_register_region(&sim->bus, 0x00000000, FLASH_SIZE,
                        &sim->memory, memory_flash_read, memory_flash_write);
    bus_register_region(&sim->bus, FLASH_BASE, FLASH_SIZE,
                        &sim->memory, memory_flash_read, memory_flash_write);

    /* Register SRAM */
    bus_register_region(&sim->bus, SRAM_BASE, SRAM_SIZE,
                        &sim->memory, memory_sram_read, memory_sram_write);

    /* Initialize peripherals */
    timer_init(&sim->timer, &sim->nvic, TIM2_IRQ);
    uart_init(&sim->uart, &sim->nvic, USART1_IRQ);
    uart_set_output(&sim->uart, default_uart_output, NULL);

    /* Register peripherals on bus */
    Peripheral tim_p = timer_as_peripheral(&sim->timer);
    Peripheral uart_p = uart_as_peripheral(&sim->uart);

    bus_register_region(&sim->bus, TIM2_BASE, TIM2_SIZE,
                        tim_p.ctx, tim_p.read, tim_p.write);
    bus_register_region(&sim->bus, USART1_BASE, USART1_SIZE,
                        uart_p.ctx, uart_p.read, uart_p.write);

    /* Store peripherals for ticking */
    sim->peripherals[0] = tim_p;
    sim->peripherals[1] = uart_p;
    sim->num_peripherals = 2;

    /* Initialize core (depends on bus and nvic) */
    core_init(&sim->core, &sim->bus, &sim->nvic);

    /* Initialize debugger */
    debugger_init(&sim->debugger);

    sim->halted  = 0;
    sim->running = 0;
}

void simulator_reset(Simulator* sim)
{
    /* Reset peripherals */
    for (int i = 0; i < sim->num_peripherals; i++) {
        if (sim->peripherals[i].reset) {
            sim->peripherals[i].reset(sim->peripherals[i].ctx);
        }
    }

    nvic_reset(&sim->nvic);
    memory_reset(&sim->memory);
    core_reset(&sim->core);

    sim->halted  = 0;
    sim->running = 0;

    printf("Simulator reset\n");
}

Status simulator_step(Simulator* sim)
{
    if (sim->halted) {
        return STATUS_HALTED;
    }

    /* 1. Tick all peripherals */
    for (int i = 0; i < sim->num_peripherals; i++) {
        if (sim->peripherals[i].tick) {
            sim->peripherals[i].tick(sim->peripherals[i].ctx);
        }
    }

    /* 2. Execute one core instruction */
    Status s = core_step(&sim->core);
    if (s != STATUS_OK) {
        sim->halted = 1;
        return s;
    }

    /* 3. Check breakpoints */
    if (debugger_check(&sim->debugger, sim->core.state.r[REG_PC])) {
        sim->halted = 1;
        printf("Breakpoint hit at 0x%08X\n", sim->core.state.r[REG_PC]);
        return STATUS_BREAKPOINT_HIT;
    }

    return STATUS_OK;
}

void simulator_run(Simulator* sim)
{
    sim->running = 1;
    sim->halted  = 0;

    while (sim->running && !sim->halted) {
        Status s = simulator_step(sim);
        if (s != STATUS_OK && s != STATUS_BREAKPOINT_HIT) {
            fprintf(stderr, "Simulation error: status=%d at PC=0x%08X\n",
                    s, sim->core.state.r[REG_PC]);
            break;
        }
        if (s == STATUS_BREAKPOINT_HIT) {
            break;
        }
    }

    sim->running = 0;
}

void simulator_halt(Simulator* sim)
{
    sim->running = 0;
    sim->halted  = 1;
}

Status simulator_load(Simulator* sim, const char* path)
{
    Status s = memory_load_binary(&sim->memory, path);
    if (s == STATUS_OK) {
        simulator_reset(sim);
    }
    return s;
}

Status simulator_add_peripheral(Simulator* sim, Peripheral periph,
                                uint32_t bus_base, uint32_t bus_size)
{
    if (sim->num_peripherals >= SIM_MAX_PERIPHERALS) {
        fprintf(stderr, "Error: peripheral limit reached\n");
        return STATUS_ERROR;
    }

    /* Register on bus if address range is provided */
    if (bus_base != 0 && bus_size != 0) {
        Status s = bus_register_region(&sim->bus, bus_base, bus_size,
                                       periph.ctx, periph.read, periph.write);
        if (s != STATUS_OK) return s;
    }

    /* Add to tickable peripherals list */
    sim->peripherals[sim->num_peripherals++] = periph;
    return STATUS_OK;
}
