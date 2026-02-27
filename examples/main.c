#include <stdint.h>

/* ===== USART1 registers ===== */
#define USART1_BASE  0x40013800U
#define USART1_SR    (*(volatile uint32_t*)(USART1_BASE + 0x00))
#define USART1_DR    (*(volatile uint32_t*)(USART1_BASE + 0x04))
#define USART1_CR1   (*(volatile uint32_t*)(USART1_BASE + 0x0C))

/* ===== TIM2 registers ===== */
#define TIM2_BASE    0x40000000U
#define TIM2_CR1     (*(volatile uint32_t*)(TIM2_BASE + 0x00))
#define TIM2_SR      (*(volatile uint32_t*)(TIM2_BASE + 0x10))
#define TIM2_CNT     (*(volatile uint32_t*)(TIM2_BASE + 0x24))
#define TIM2_PSC     (*(volatile uint32_t*)(TIM2_BASE + 0x28))
#define TIM2_ARR     (*(volatile uint32_t*)(TIM2_BASE + 0x2C))

static void uart_init(void)
{
     USART1_CR1 = (1U << 13) | (1U << 3);  /* UE | TE */
}

static void uart_putc(char c)
{
    USART1_DR = c;
    while (!(USART1_SR & (1U << 7)))      /* Wait for TXE */
        ;
}

static void uart_puts(const char* s)
{
    while (*s)
        uart_putc(*s++);
}

static void timer_init(uint32_t period)
{
    TIM2_ARR = period;
    TIM2_PSC = 0;
    TIM2_CR1 = 1;  /* CEN — start counting */
}

int main(void)
{
    uart_init();
    uart_puts("Hello from STM32!\n");

    timer_init(100);

    int overflows = 0;
    while (overflows < 3) {
        if (TIM2_SR & 1U) {   /* UIF — overflow happened? */
            TIM2_SR = 0;      /* Clear flag */
            overflows++;
            uart_putc('0' + overflows);
            uart_putc('\n');
        }
    }

    uart_puts("Done!\n");

    while (1)
        ;
}
