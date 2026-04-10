#include "simulator/simulator.h"
#include "events/event_queue.h"
#include "peripherals/systick/systick.h"
#include "peripherals/rcc/rcc.h"
#include "peripherals/gpio/gpio.h"
#include "nvic/nvic_bus.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>

static NvicBusState g_nvic_bus;

static volatile sig_atomic_t g_sim_interrupted = 0;

static void sigint_handler(int sig)
{
    (void)sig;
    g_sim_interrupted = 1;
}

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
    event_queue_init(&sim->events);

    /* Register flash at 0x00000000 (alias) and 0x08000000 */
    bus_register_region(&sim->bus, 0x00000000, FLASH_SIZE,
                        &sim->memory, memory_flash_read, memory_flash_write);
    bus_register_region(&sim->bus, FLASH_BASE, FLASH_SIZE,
                        &sim->memory, memory_flash_read, memory_flash_write);

    /* Register SRAM */
    bus_register_region(&sim->bus, SRAM_BASE, SRAM_SIZE,
                        &sim->memory, memory_sram_read, memory_sram_write);

    /* Initialize peripherals */
    timer_init(&sim->timer, &sim->nvic, TIM2_IRQ, &sim->events, &sim->cycle);
    systick_init(&sim->systick, &sim->nvic, &sim->events, &sim->cycle);
    rcc_init(&sim->rcc);
    gpio_init(&sim->gpioa);
    gpio_init(&sim->gpiob);
    gpio_init(&sim->gpioc);
    uart_init(&sim->uart, &sim->nvic, USART1_IRQ);
    uart_set_output(&sim->uart, default_uart_output, NULL);

    /* Attach logger to UART */
    uart_logger_init(&sim->uart_logger);
    sim->uart.logger = &sim->uart_logger;

    /* Register peripherals on bus */
    Peripheral tim_p     = timer_as_peripheral(&sim->timer);
    Peripheral systick_p = systick_as_peripheral(&sim->systick);
    Peripheral rcc_p     = rcc_as_peripheral(&sim->rcc);
    Peripheral gpioa_p   = gpio_as_peripheral(&sim->gpioa);
    Peripheral gpiob_p   = gpio_as_peripheral(&sim->gpiob);
    Peripheral gpioc_p   = gpio_as_peripheral(&sim->gpioc);
    Peripheral uart_p    = uart_as_peripheral(&sim->uart);

    bus_register_region(&sim->bus, TIM2_BASE,   TIM2_SIZE,   tim_p.ctx,     tim_p.read,     tim_p.write);
    bus_register_region(&sim->bus, SYST_BASE,   SYST_SIZE,   systick_p.ctx, systick_p.read, systick_p.write);
    bus_register_region(&sim->bus, RCC_BASE,    RCC_SIZE,    rcc_p.ctx,     rcc_p.read,     rcc_p.write);
    bus_register_region(&sim->bus, GPIOA_BASE,  GPIO_SIZE,   gpioa_p.ctx,   gpioa_p.read,   gpioa_p.write);
    bus_register_region(&sim->bus, GPIOB_BASE,  GPIO_SIZE,   gpiob_p.ctx,   gpiob_p.read,   gpiob_p.write);
    bus_register_region(&sim->bus, GPIOC_BASE,  GPIO_SIZE,   gpioc_p.ctx,   gpioc_p.read,   gpioc_p.write);
    bus_register_region(&sim->bus, USART1_BASE, USART1_SIZE, uart_p.ctx,    uart_p.read,    uart_p.write);

    nvic_bus_init(&g_nvic_bus, &sim->nvic);
    Peripheral nvic_bus_p = nvic_bus_as_peripheral(&g_nvic_bus);
    bus_register_region(&sim->bus, NVIC_BUS_BASE, NVIC_BUS_SIZE,
                        nvic_bus_p.ctx, nvic_bus_p.read, nvic_bus_p.write);

    /* Store peripherals for reset dispatch */
    sim->peripherals[0] = tim_p;
    sim->peripherals[1] = systick_p;
    sim->peripherals[2] = rcc_p;
    sim->peripherals[3] = gpioa_p;
    sim->peripherals[4] = gpiob_p;
    sim->peripherals[5] = gpioc_p;
    sim->peripherals[6] = uart_p;
    sim->num_peripherals = 7;

    /* Initialize core (depends on bus and nvic) */
    core_init(&sim->core, &sim->bus, &sim->nvic);
    profiler_init(&sim->profiler);
    sim->core.profiler = &sim->profiler;

    /* Initialize debugger */
    debugger_init(&sim->debugger);

    sim->cycle   = 0;
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
    event_queue_init(&sim->events);  /* Flush all pending events on reset */

    profiler_reset(&sim->profiler);
    sim->cycle   = 0;
    sim->halted  = 0;
    sim->running = 0;

    printf("Simulator reset\n");
}

Status simulator_step(Simulator* sim)
{
    if (sim->halted) {
        return STATUS_HALTED;
    }

    /* 1. Advance cycle counter */
    sim->cycle++;

    /* 2. Tick polling-based peripherals (those without event queue support) */
    for (int i = 0; i < sim->num_peripherals; i++) {
        if (sim->peripherals[i].tick) {
            sim->peripherals[i].tick(sim->peripherals[i].ctx);
        }
    }

    /* 3. Fire any events scheduled for this cycle (before the instruction
     *    so that IRQs raised here are visible to core_step) */
    event_queue_dispatch(&sim->events, sim->cycle);

    /* 4. Execute one core instruction */
    Status s = core_step(&sim->core);
    if (s != STATUS_OK) {
        sim->halted = 1;
        return s;
    }

    /* 5. Check breakpoints */
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
    g_sim_interrupted = 0;

    signal(SIGINT, sigint_handler);

    while (sim->running && !sim->halted && !g_sim_interrupted) {
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

    signal(SIGINT, SIG_DFL);
    sim->running = 0;

    if (g_sim_interrupted) {
        printf("\nInterrupted — type 'diagram' to view UART log\n");
        g_sim_interrupted = 0;
    }
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
