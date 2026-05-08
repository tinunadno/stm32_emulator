#include <stdint.h>

#define USART1_BASE  0x40013800U
#define USART1_SR  (*(volatile uint32_t*)(USART1_BASE + 0x00))
#define USART1_DR  (*(volatile uint32_t*)(USART1_BASE + 0x04))
#define USART1_CR1 (*(volatile uint32_t*)(USART1_BASE + 0x0C))

#define RCC_APB2ENR (*(volatile uint32_t*)(0x40021018U))
#define RCC_APB2ENR_USART1EN (1U << 14)

#define USART1_CR1_UE  (1U << 13)
#define USART1_CR1_TE  (1U <<  3)
#define USART1_SR_TXE  (1U <<  7)

static void uart_putc(char c)
{
    USART1_DR = (uint32_t)c;
    while (!(USART1_SR & USART1_SR_TXE))
        ;
}

static void uart_puts(const char* s)
{
    while (*s) uart_putc(*s++);
}

int main(void)
{
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART1_CR1   = USART1_CR1_UE | USART1_CR1_TE;

    uart_puts("Hello, World!\r\n");
    uart_puts("STM32 simulator works!\r\n");

    while (1)
        ;
}
