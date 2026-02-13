#ifndef STM32_UART_H
#define STM32_UART_H

#include <stdint.h>
#include "common/status.h"
#include "nvic/nvic.h"
#include "peripherals/peripheral.h"

/* USART register offsets (from base 0x40013800) */
#define UART_SR_OFFSET   0x00
#define UART_DR_OFFSET   0x04
#define UART_BRR_OFFSET  0x08
#define UART_CR1_OFFSET  0x0C

/* SR bits */
#define UART_SR_TXE      (1U << 7)   /* Transmit data register empty */
#define UART_SR_TC       (1U << 6)   /* Transmission complete */
#define UART_SR_RXNE     (1U << 5)   /* Read data register not empty */

/* CR1 bits */
#define UART_CR1_UE      (1U << 13)  /* USART enable */
#define UART_CR1_TXEIE   (1U << 7)   /* TXE interrupt enable */
#define UART_CR1_TCIE    (1U << 6)   /* TC interrupt enable */
#define UART_CR1_RXNEIE  (1U << 5)   /* RXNE interrupt enable */
#define UART_CR1_TE      (1U << 3)   /* Transmitter enable */
#define UART_CR1_RE      (1U << 2)   /* Receiver enable */

/* Output callback: called when UART transmits a character */
typedef void (*UartOutputFn)(char c, void* user_data);

#define UART_RX_BUFFER_SIZE 16

/**
 * USART peripheral.
 */
typedef struct {
    uint32_t     sr;
    uint32_t     dr;
    uint32_t     brr;
    uint32_t     cr1;
    NVIC*        nvic;
    uint32_t     irq;
    /* TX state */
    int          tx_pending;       /* Character waiting to be transmitted */
    uint8_t      tx_char;
    /* RX buffer (circular) */
    uint8_t      rx_buffer[UART_RX_BUFFER_SIZE];
    int          rx_head;
    int          rx_tail;
    int          rx_count;
    /* Output callback */
    UartOutputFn output_fn;
    void*        output_user_data;
} UartState;

void uart_init(UartState* uart, NVIC* nvic, uint32_t irq);

/** Set the callback for transmitted characters. */
void uart_set_output(UartState* uart, UartOutputFn fn, void* user_data);

/** Feed an incoming character into the UART receive buffer. */
void uart_incoming_char(UartState* uart, char c);

/** Fill a Peripheral struct for bus/simulator registration. */
Peripheral uart_as_peripheral(UartState* uart);

/* Peripheral-compatible callbacks */
uint32_t uart_read(void* ctx, uint32_t offset, uint8_t size);
Status   uart_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size);
void     uart_tick(void* ctx);
void     uart_reset(void* ctx);

#endif /* STM32_UART_H */
