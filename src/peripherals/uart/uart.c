#include "peripherals/uart/uart.h"
#include <string.h>
#include <stdio.h>

void uart_init(UartState* uart, NVIC* nvic, uint32_t irq)
{
    memset(uart, 0, sizeof(UartState));
    uart->nvic = nvic;
    uart->irq  = irq;
    uart->sr   = UART_SR_TXE | UART_SR_TC;  /* TX ready by default */
}

void uart_set_output(UartState* uart, UartOutputFn fn, void* user_data)
{
    uart->output_fn        = fn;
    uart->output_user_data = user_data;
}

Peripheral uart_as_peripheral(UartState* uart)
{
    Peripheral p;
    p.ctx   = uart;
    p.read  = uart_read;
    p.write = uart_write;
    p.tick  = uart_tick;
    p.reset = uart_reset;
    return p;
}

void uart_reset(void* ctx)
{
    UartState* uart = (UartState*)ctx;
    NVIC* nvic        = uart->nvic;
    uint32_t irq      = uart->irq;
    UartOutputFn fn    = uart->output_fn;
    void* ud           = uart->output_user_data;

    memset(uart, 0, sizeof(UartState));
    uart->nvic             = nvic;
    uart->irq              = irq;
    uart->sr               = UART_SR_TXE | UART_SR_TC;
    uart->output_fn        = fn;
    uart->output_user_data = ud;
}

uint32_t uart_read(void* ctx, uint32_t offset, uint8_t size)
{
    UartState* uart = (UartState*)ctx;
    (void)size;

    switch (offset) {
    case UART_SR_OFFSET:
        return uart->sr;

    case UART_DR_OFFSET: {
        /* Reading DR returns received data and clears RXNE */
        uint8_t data = 0;
        if (uart->rx_count > 0) {
            data = uart->rx_buffer[uart->rx_tail];
            uart->rx_tail = (uart->rx_tail + 1) % UART_RX_BUFFER_SIZE;
            uart->rx_count--;
            if (uart->rx_count == 0) {
                uart->sr &= ~UART_SR_RXNE;
            }
        }
        return data;
    }

    case UART_BRR_OFFSET:
        return uart->brr;

    case UART_CR1_OFFSET:
        return uart->cr1;

    default:
        fprintf(stderr, "UART: read from unknown offset 0x%02X\n", offset);
        return 0;
    }
}

Status uart_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size)
{
    UartState* uart = (UartState*)ctx;
    (void)size;

    switch (offset) {
    case UART_SR_OFFSET:
        uart->sr &= value;  /* Write-0-to-clear for applicable bits */
        break;

    case UART_DR_OFFSET:
        /* Writing DR starts a transmission */
        if (uart->cr1 & UART_CR1_UE) {
            uart->tx_char    = (uint8_t)(value & 0xFF);
            uart->tx_pending = 1;
            uart->sr &= ~(UART_SR_TXE | UART_SR_TC);
        }
        break;

    case UART_BRR_OFFSET:
        uart->brr = value;
        break;

    case UART_CR1_OFFSET:
        uart->cr1 = value;
        break;

    default:
        fprintf(stderr, "UART: write to unknown offset 0x%02X\n", offset);
        return STATUS_ERROR;
    }
    return STATUS_OK;
}

void uart_incoming_char(UartState* uart, char c)
{
    if (uart->rx_count >= UART_RX_BUFFER_SIZE) {
        fprintf(stderr, "UART: RX buffer overflow, character dropped\n");
        return;
    }

    uart->rx_buffer[uart->rx_head] = (uint8_t)c;
    uart->rx_head = (uart->rx_head + 1) % UART_RX_BUFFER_SIZE;
    uart->rx_count++;
    uart->sr |= UART_SR_RXNE;

    /* Generate interrupt if enabled */
    if ((uart->cr1 & UART_CR1_RXNEIE) && (uart->cr1 & UART_CR1_UE)) {
        nvic_set_pending(uart->nvic, uart->irq);
    }
}

void uart_tick(void* ctx)
{
    UartState* uart = (UartState*)ctx;

    if (!uart->tx_pending)
        return;

    /* Complete the transmission (instant in simulation) */
    uart->tx_pending = 0;

    /* Call output callback */
    if (uart->output_fn) {
        uart->output_fn((char)uart->tx_char, uart->output_user_data);
    }

    /* Set status flags */
    uart->sr |= UART_SR_TXE | UART_SR_TC;

    /* Generate TXE interrupt if enabled */
    if ((uart->cr1 & UART_CR1_TXEIE) && (uart->cr1 & UART_CR1_UE)) {
        nvic_set_pending(uart->nvic, uart->irq);
    }
}
