#include "tests/test.h"
#include "peripherals/uart/uart.h"
#include "nvic/nvic.h"

/* --- Capture callback for testing TX output --- */
static char captured_char;
static int  capture_count;

static void capture_output(char c, void* user_data)
{
    (void)user_data;
    captured_char = c;
    capture_count++;
}

/* --- Test: UART TX writes character and calls callback --- */
static void test_uart_tx(void)
{
    NVIC nvic;
    UartState uart;
    nvic_init(&nvic);
    uart_init(&uart, &nvic, 37);
    uart_set_output(&uart, capture_output, NULL);

    captured_char = 0;
    capture_count = 0;

    /* Enable UART and transmitter */
    uart.cr1 = UART_CR1_UE | UART_CR1_TE;

    /* Write character to DR */
    uart_write(&uart, UART_DR_OFFSET, 'A', 4);

    /* Before tick: TXE should be cleared */
    ASSERT(!(uart.sr & UART_SR_TXE));

    /* Tick: transmission completes */
    uart_tick(&uart);

    /* Verify: callback called, flags set */
    ASSERT_EQ(capture_count, 1);
    ASSERT_EQ(captured_char, 'A');
    ASSERT(uart.sr & UART_SR_TXE);
    ASSERT(uart.sr & UART_SR_TC);
}

/* --- Test: UART RX receives character via incoming_char --- */
static void test_uart_rx(void)
{
    NVIC nvic;
    UartState uart;
    nvic_init(&nvic);
    uart_init(&uart, &nvic, 37);

    uart.cr1 = UART_CR1_UE | UART_CR1_RE;

    /* Initially no data */
    ASSERT(!(uart.sr & UART_SR_RXNE));

    /* Feed a character */
    uart_incoming_char(&uart, 'Z');

    /* RXNE should be set */
    ASSERT(uart.sr & UART_SR_RXNE);

    /* Read DR → should return 'Z' */
    uint32_t data = uart_read(&uart, UART_DR_OFFSET, 4);
    ASSERT_EQ(data, 'Z');

    /* After reading, RXNE should be cleared (buffer empty) */
    ASSERT(!(uart.sr & UART_SR_RXNE));
}

/* --- Test: UART TX generates interrupt when TXEIE is set --- */
static void test_uart_tx_irq(void)
{
    NVIC nvic;
    UartState uart;
    nvic_init(&nvic);
    uart_init(&uart, &nvic, 37);
    uart_set_output(&uart, capture_output, NULL);

    captured_char = 0;
    capture_count = 0;

    uart.cr1 = UART_CR1_UE | UART_CR1_TE | UART_CR1_TXEIE;
    nvic_enable_irq(&nvic, 37);

    /* Write and tick */
    uart_write(&uart, UART_DR_OFFSET, 'B', 4);
    uart_tick(&uart);

    /* NVIC should have IRQ 37 pending */
    ASSERT(nvic.pending[37]);
    ASSERT_EQ(captured_char, 'B');
}

/* --- Test: UART RX generates interrupt when RXNEIE is set --- */
static void test_uart_rx_irq(void)
{
    NVIC nvic;
    UartState uart;
    nvic_init(&nvic);
    uart_init(&uart, &nvic, 37);

    uart.cr1 = UART_CR1_UE | UART_CR1_RE | UART_CR1_RXNEIE;
    nvic_enable_irq(&nvic, 37);

    uart_incoming_char(&uart, 'X');

    ASSERT(nvic.pending[37]);
}

/* --- Test: UART RX buffer handles multiple characters --- */
static void test_uart_rx_buffer(void)
{
    NVIC nvic;
    UartState uart;
    nvic_init(&nvic);
    uart_init(&uart, &nvic, 37);

    uart.cr1 = UART_CR1_UE | UART_CR1_RE;

    uart_incoming_char(&uart, 'H');
    uart_incoming_char(&uart, 'i');
    uart_incoming_char(&uart, '!');

    ASSERT_EQ(uart.rx_count, 3);

    /* Read in order (FIFO) */
    ASSERT_EQ(uart_read(&uart, UART_DR_OFFSET, 4), 'H');
    ASSERT_EQ(uart_read(&uart, UART_DR_OFFSET, 4), 'i');
    ASSERT_EQ(uart_read(&uart, UART_DR_OFFSET, 4), '!');

    ASSERT_EQ(uart.rx_count, 0);
    ASSERT(!(uart.sr & UART_SR_RXNE));
}

void test_uart_all(void)
{
    TEST_SUITE("UART (USART1)");
    RUN_TEST(test_uart_tx);
    RUN_TEST(test_uart_rx);
    RUN_TEST(test_uart_tx_irq);
    RUN_TEST(test_uart_rx_irq);
    RUN_TEST(test_uart_rx_buffer);
}
