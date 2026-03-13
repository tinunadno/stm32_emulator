#include "tests/test.h"
#include "simulator/simulator.h"

/* TIM2 IRQ = 28, vector index = 16 + 28 = 44, vector table offset = 44 * 4 = 0xB0 */
#define TIM2_IRQ       28
#define TIM2_VECTOR    44
#define TIM2_VEC_OFF   (TIM2_VECTOR * 4)

#define TIM2_BASE_ADDR 0x40000000U
#define TIM_CR1_OFF    0x00
#define TIM_DIER_OFF   0x0C
#define TIM_ARR_OFF    0x2C
#define TIM_PSC_OFF    0x28

/**
 * Integration test: Timer overflow triggers IRQ handler.
 *
 * Memory layout:
 *   Vector table:
 *     [0x00] = 0x20004FF0    (initial SP, safely inside SRAM)
 *     [0x04] = 0x08000081    (reset handler at 0x80, Thumb)
 *     [0xB0] = 0x080000C1    (TIM2 handler at 0xC0, Thumb)
 *
 *   Reset handler at 0x80:
 *     MOV R4, #0          ; R4 = 0 (flag: "handler not called")
 *     B .                 ; infinite loop
 *
 *   TIM2 IRQ handler at 0xC0:
 *     MOV R4, #1          ; R4 = 1 (flag: "handler was called!")
 *     BX LR               ; return from exception
 *
 * After configuring TIM2 to overflow and running enough steps,
 * R4 should be 1 — proving the full path:
 *   timer tick → overflow → NVIC pending → exception entry →
 *   handler executes → exception return → main loop resumes
 */
static void test_integration_timer_irq_handler(void)
{
    Simulator sim;
    simulator_init(&sim);

    /* --- Write firmware into flash --- */

    /* Vector table */
    flash_write32(&sim.memory, 0x00, 0x20004FF0);  /* Initial SP */
    flash_write32(&sim.memory, 0x04, 0x08000081);  /* Reset handler (Thumb) */
    flash_write32(&sim.memory, TIM2_VEC_OFF, 0x080000C1); /* TIM2 handler (Thumb) */

    /* Reset handler at 0x80 */
    flash_write16(&sim.memory, 0x80, 0x2400);  /* MOV R4, #0 */
    flash_write16(&sim.memory, 0x82, 0xE7FE);  /* B . */

    /* TIM2 IRQ handler at 0xC0 */
    flash_write16(&sim.memory, 0xC0, 0x2401);  /* MOV R4, #1 */
    flash_write16(&sim.memory, 0xC2, 0x4770);  /* BX LR */

    /* --- Reset CPU (loads SP and PC from vector table) --- */
    simulator_reset(&sim);

    ASSERT_EQ(sim.core.state.r[REG_SP], 0x20004FF0);
    ASSERT_EQ(sim.core.state.r[REG_PC], 0x08000080);

    /* --- Configure TIM2 via bus (simulating firmware setup) --- */
    bus_write(&sim.bus, TIM2_BASE_ADDR + TIM_ARR_OFF,  5, 4);  /* ARR = 5 */
    bus_write(&sim.bus, TIM2_BASE_ADDR + TIM_PSC_OFF,  0, 4);  /* PSC = 0 */
    bus_write(&sim.bus, TIM2_BASE_ADDR + TIM_DIER_OFF, 1, 4);  /* UIE = 1 */
    bus_write(&sim.bus, TIM2_BASE_ADDR + TIM_CR1_OFF,  1, 4);  /* CEN = 1 */

    /* Enable TIM2 IRQ in NVIC */
    nvic_enable_irq(&sim.nvic, TIM2_IRQ);

    /* --- Execute: step 1 → MOV R4, #0 --- */
    simulator_step(&sim);
    ASSERT_EQ(sim.core.state.r[4], 0);

    /* --- Step 2..4: timer counting, CPU in B . loop --- */
    simulator_step(&sim);
    simulator_step(&sim);
    simulator_step(&sim);
    ASSERT_EQ(sim.core.state.r[4], 0);  /* Still no overflow */

    /*
     * Step 5: timer overflow! CNT reaches ARR=5, sets UIF and pending.
     * core_step executes B ., then sees pending IRQ → enters exception.
     * After this step, PC should be at the handler (0xC0).
     */
    simulator_step(&sim);

    /* Verify we entered the exception handler */
    ASSERT_EQ(sim.core.state.r[REG_PC], 0x080000C0);
    ASSERT(sim.core.state.current_irq > 0);

    /* --- Step 6: execute MOV R4, #1 in handler --- */
    simulator_step(&sim);
    ASSERT_EQ(sim.core.state.r[4], 1);

    /* --- Step 7: BX LR → return from exception --- */
    simulator_step(&sim);

    /* Verify: back in the main loop */
    ASSERT_EQ(sim.core.state.r[REG_PC], 0x08000082);  /* B . instruction */
    ASSERT_EQ(sim.core.state.current_irq, 0);          /* No active IRQ */

    /* R4 should still be 1 (handler's value persists, R4 not in exception frame) */
    ASSERT_EQ(sim.core.state.r[4], 1);

    /* Timer UIF should have been set */
    ASSERT(sim.timer.sr & TIM_SR_UIF);

    /* Verify the cycle count shows real work was done */
    ASSERT(sim.core.state.cycles >= 7);
}

/**
 * Integration test: Breakpoint halts execution.
 */
static void test_integration_breakpoint_halt(void)
{
    Simulator sim;
    simulator_init(&sim);

    /* Simple program: increment R0 in a loop */
    flash_write32(&sim.memory, 0x00, 0x20004FF0);
    flash_write32(&sim.memory, 0x04, 0x08000081);

    /* 0x80: MOV R0, #0
     * 0x82: ADD R0, #1
     * 0x84: ADD R0, #1
     * 0x86: ADD R0, #1
     * 0x88: B .
     */
    flash_write16(&sim.memory, 0x80, 0x2000);  /* MOV R0, #0 */
    flash_write16(&sim.memory, 0x82, 0x3001);  /* ADD R0, #1 */
    flash_write16(&sim.memory, 0x84, 0x3001);  /* ADD R0, #1 */
    flash_write16(&sim.memory, 0x86, 0x3001);  /* ADD R0, #1 */
    flash_write16(&sim.memory, 0x88, 0xE7FE);  /* B . */

    simulator_reset(&sim);

    /* Set breakpoint at 0x08000086 (third ADD) */
    debugger_add_breakpoint(&sim.debugger, 0x08000086);

    /* Run: should stop at breakpoint */
    simulator_run(&sim);

    ASSERT_EQ(sim.core.state.r[REG_PC], 0x08000086);
    ASSERT_EQ(sim.core.state.r[0], 2);  /* Two ADDs executed */
    ASSERT(sim.halted);
}

/**
 * Integration test: UART TX outputs character during simulation.
 */
static char uart_test_output;
static int  uart_test_count;

static void uart_test_callback(char c, void* user_data)
{
    (void)user_data;
    uart_test_output = c;
    uart_test_count++;
}

static void test_integration_uart_output(void)
{
    Simulator sim;
    simulator_init(&sim);
    uart_set_output(&sim.uart, uart_test_callback, NULL);

    uart_test_output = 0;
    uart_test_count  = 0;

    /* Simple firmware that loops */
    flash_write32(&sim.memory, 0x00, 0x20004FF0);
    flash_write32(&sim.memory, 0x04, 0x08000081);
    flash_write16(&sim.memory, 0x80, 0xE7FE);  /* B . */

    simulator_reset(&sim);

    /* Configure UART and write a character via bus */
    bus_write(&sim.bus, 0x40013800 + 0x0C, UART_CR1_UE | UART_CR1_TE, 4); /* CR1 */
    bus_write(&sim.bus, 0x40013800 + 0x04, 'Q', 4);  /* DR = 'Q' */

    /* Step: UART tick should transmit */
    simulator_step(&sim);

    ASSERT_EQ(uart_test_count, 1);
    ASSERT_EQ(uart_test_output, 'Q');
}

void test_integration_all(void)
{
    TEST_SUITE("Integration");
    RUN_TEST(test_integration_timer_irq_handler);
    RUN_TEST(test_integration_breakpoint_halt);
    RUN_TEST(test_integration_uart_output);
}
