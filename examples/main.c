#include <stdint.h>

/* =========================================================
 * Register map — matches the emulator's peripheral layout
 * ========================================================= */

/* RCC — 0x40021000 */
#define RCC_BASE        0x40021000U
#define RCC_CR       (*(volatile uint32_t*)(RCC_BASE + 0x00))
#define RCC_CFGR     (*(volatile uint32_t*)(RCC_BASE + 0x04))
#define RCC_APB2ENR  (*(volatile uint32_t*)(RCC_BASE + 0x18))
#define RCC_APB1ENR  (*(volatile uint32_t*)(RCC_BASE + 0x1C))

#define RCC_CR_HSION      (1U <<  0)
#define RCC_CR_HSIRDY     (1U <<  1)
#define RCC_CR_PLLON      (1U << 24)
#define RCC_CR_PLLRDY     (1U << 25)
#define RCC_CFGR_SW_PLL   (2U <<  0)   /* SW = PLL as clock source */
#define RCC_CFGR_SWS_MASK (3U <<  2)   /* SWS read-back bits */
#define RCC_CFGR_SWS_PLL  (2U <<  2)

#define RCC_APB2ENR_IOPAEN    (1U <<  2)
#define RCC_APB2ENR_IOPBEN    (1U <<  3)
#define RCC_APB2ENR_USART1EN  (1U << 14)
#define RCC_APB1ENR_TIM2EN    (1U <<  0)

/* GPIOA — 0x40010800 */
#define GPIOA_BASE      0x40010800U
#define GPIOA_CRL    (*(volatile uint32_t*)(GPIOA_BASE + 0x00))
#define GPIOA_ODR    (*(volatile uint32_t*)(GPIOA_BASE + 0x0C))
#define GPIOA_BSRR   (*(volatile uint32_t*)(GPIOA_BASE + 0x10))

/* GPIOB — 0x40010C00 */
#define GPIOB_BASE      0x40010C00U
#define GPIOB_CRL    (*(volatile uint32_t*)(GPIOB_BASE + 0x00))
#define GPIOB_ODR    (*(volatile uint32_t*)(GPIOB_BASE + 0x0C))
#define GPIOB_BSRR   (*(volatile uint32_t*)(GPIOB_BASE + 0x10))

/* SysTick — 0xE000E010 */
#define SYST_BASE       0xE000E010U
#define SYST_CSR     (*(volatile uint32_t*)(SYST_BASE + 0x00))
#define SYST_RVR     (*(volatile uint32_t*)(SYST_BASE + 0x04))
#define SYST_CVR     (*(volatile uint32_t*)(SYST_BASE + 0x08))

#define SYST_CSR_ENABLE     (1U << 0)
#define SYST_CSR_TICKINT    (1U << 1)   /* generate SysTick exception */
#define SYST_CSR_CLKSOURCE  (1U << 2)   /* processor clock */

/* TIM2 — 0x40000000 */
#define TIM2_BASE       0x40000000U
#define TIM2_CR1     (*(volatile uint32_t*)(TIM2_BASE + 0x00))
#define TIM2_DIER    (*(volatile uint32_t*)(TIM2_BASE + 0x0C))
#define TIM2_SR      (*(volatile uint32_t*)(TIM2_BASE + 0x10))
#define TIM2_CNT     (*(volatile uint32_t*)(TIM2_BASE + 0x24))
#define TIM2_PSC     (*(volatile uint32_t*)(TIM2_BASE + 0x28))
#define TIM2_ARR     (*(volatile uint32_t*)(TIM2_BASE + 0x2C))

#define TIM2_CR1_CEN     (1U << 0)
#define TIM2_SR_UIF      (1U << 0)
#define TIM2_DIER_UIE    (1U << 0)   /* update interrupt enable */

/* USART1 — 0x40013800 */
#define USART1_BASE     0x40013800U
#define USART1_SR    (*(volatile uint32_t*)(USART1_BASE + 0x00))
#define USART1_DR    (*(volatile uint32_t*)(USART1_BASE + 0x04))
#define USART1_CR1   (*(volatile uint32_t*)(USART1_BASE + 0x0C))

#define USART1_CR1_UE  (1U << 13)
#define USART1_CR1_TE  (1U <<  3)
#define USART1_SR_TXE  (1U <<  7)

/* NVIC — 0xE000E100 (Cortex-M3 register interface) */
#define NVIC_BASE       0xE000E100U
#define NVIC_ISER0   (*(volatile uint32_t*)(NVIC_BASE + 0x000))  /* enable  IRQ 0-31 */
#define NVIC_ICER0   (*(volatile uint32_t*)(NVIC_BASE + 0x080))  /* disable IRQ 0-31 */
#define NVIC_IPR7    (*(volatile uint32_t*)(NVIC_BASE + 0x31C))  /* priority IRQ28-31*/

/* TIM2 is IRQ28.  SysTick priority stays at 0 (default = highest). */
#define TIM2_IRQ_BIT    (1U << 28)
#define TIM2_PRIORITY   64U   /* lower number = higher priority; 64 < SysTick(0)? */
                              /* Actually higher number = lower priority in Cortex-M */

/* =========================================================
 * Shared state modified by ISRs  (volatile)
 * ========================================================= */
static volatile int systick_cnt = 0;
static volatile int tim2_cnt    = 0;

/* =========================================================
 * UART helpers
 * ========================================================= */

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

static void uart_puthex8(uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    uart_putc(h[(v >> 4) & 0xF]);
    uart_putc(h[ v       & 0xF]);
}

static void uart_puthex32(uint32_t v)
{
    uart_puts("0x");
    uart_puthex8(v >> 24);
    uart_puthex8(v >> 16);
    uart_puthex8(v >>  8);
    uart_puthex8(v);
}

static void uart_putdec(uint32_t n)
{
    if (n == 0) { uart_putc('0'); return; }
    char buf[10];
    int i = 0;
    while (n > 0) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i > 0) uart_putc(buf[--i]);
}

/* =========================================================
 * Interrupt Service Routines
 * ========================================================= */

/*
 * SysTick exception — fires every 500 cycles (RVR=499).
 * Priority: 0 (highest, default).
 * Toggles PA5, prints status from interrupt context.
 */
void SysTick_Handler(void)
{
    systick_cnt++;

    if (systick_cnt & 1)
        GPIOA_BSRR = (1U << 5);           /* set   PA5 */
    else
        GPIOA_BSRR = (1U << (5 + 16));    /* reset PA5 */

    uint32_t pa5 = (GPIOA_ODR >> 5) & 1U;

    uart_puts("[IRQ:SysTick] #");
    uart_putdec((uint32_t)systick_cnt);
    uart_puts("  PA5=");
    uart_putc('0' + (char)pa5);
    uart_puts("  TIM2_CNT=");
    uart_putdec(TIM2_CNT);
    uart_puts("\r\n");
}

/*
 * TIM2 IRQ (IRQ28) — fires every 1000 cycles (ARR=999, PSC=0).
 * Priority: 64 (lower than SysTick=0, so SysTick can preempt TIM2).
 * Toggles PB0, clears UIF, prints status from interrupt context.
 */
void TIM2_IRQHandler(void)
{
    TIM2_SR = 0;   /* clear UIF — must be done first to re-arm */
    tim2_cnt++;

    if (tim2_cnt & 1)
        GPIOB_BSRR = (1U << 0);           /* set   PB0 */
    else
        GPIOB_BSRR = (1U << 16);          /* reset PB0 */

    uint32_t pb0 = (GPIOB_ODR >> 0) & 1U;

    uart_puts("[IRQ:TIM2]    #");
    uart_putdec((uint32_t)tim2_cnt);
    uart_puts("  PB0=");
    uart_putc('0' + (char)pb0);
    uart_puts("\r\n");
}

/* =========================================================
 * main
 * ========================================================= */

int main(void)
{
    /* -------------------------------------------------------
     * 1. USART1 — init first so we can print from the start
     * ------------------------------------------------------- */
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART1_CR1   = USART1_CR1_UE | USART1_CR1_TE;

    uart_puts("=== STM32 Peripheral Demo (interrupt-driven) ===\r\n\r\n");

    /* -------------------------------------------------------
     * 2. RCC — enable HSI, configure PLL, switch system clock
     * ------------------------------------------------------- */
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY))   /* wait for HSI ready (instant in stub) */
        ;

    RCC_CR |= RCC_CR_PLLON;
    while (!(RCC_CR & RCC_CR_PLLRDY))   /* wait for PLL lock (instant in stub) */
        ;

    RCC_CFGR = (RCC_CFGR & ~(uint32_t)3U) | RCC_CFGR_SW_PLL;
    while ((RCC_CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL)
        ;

    /* Enable peripheral clocks */
    RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;
    RCC_APB1ENR |= RCC_APB1ENR_TIM2EN;

    uart_puts("[RCC]     CR=");     uart_puthex32(RCC_CR);
    uart_puts("  CFGR=");          uart_puthex32(RCC_CFGR);
    uart_puts("  (PLL locked, PLL selected as SYSCLK)\r\n");

    /* -------------------------------------------------------
     * 3. GPIOA — PA5 as output push-pull 2 MHz (LED)
     * ------------------------------------------------------- */
    uint32_t crl = GPIOA_CRL;
    crl &= ~(0xFU << 20);
    crl |=  (0x2U << 20);   /* MODE=10 output 2 MHz, CNF=00 push-pull */
    GPIOA_CRL = crl;

    uart_puts("[GPIOA]   PA5=output  CRL="); uart_puthex32(GPIOA_CRL);
    uart_puts("\r\n");

    /* -------------------------------------------------------
     * 4. GPIOB — PB0 as output push-pull 2 MHz
     * ------------------------------------------------------- */
    uint32_t crlb = GPIOB_CRL;
    crlb &= ~(0xFU << 0);
    crlb |=  (0x2U << 0);   /* MODE=10, CNF=00 for pin 0 */
    GPIOB_CRL = crlb;

    uart_puts("[GPIOB]   PB0=output  CRL="); uart_puthex32(GPIOB_CRL);
    uart_puts("\r\n");

    /* -------------------------------------------------------
     * 5. NVIC — set TIM2 priority and enable IRQ28
     *    SysTick stays at priority 0 (highest, default).
     *    TIM2 gets priority 64 — lower, so SysTick can preempt it.
     * ------------------------------------------------------- */
    NVIC_IPR7   = (TIM2_PRIORITY << 0);   /* byte 0 = IRQ28 priority */
    NVIC_ISER0  = TIM2_IRQ_BIT;           /* enable TIM2 IRQ */

    uart_puts("[NVIC]    IRQ28(TIM2) enabled, priority=");
    uart_putdec(TIM2_PRIORITY);
    uart_puts(" (SysTick priority=0)\r\n");

    /* -------------------------------------------------------
     * 6. SysTick — interrupt every 500 cycles (TICKINT=1)
     * ------------------------------------------------------- */
    SYST_RVR = 9999U;
    SYST_CVR = 0U;
    SYST_CSR = SYST_CSR_ENABLE | SYST_CSR_TICKINT | SYST_CSR_CLKSOURCE;

    uart_puts("[SysTick] RVR=9999 TICKINT=1 (interrupt every 10000 cycles)\r\n");

    /* -------------------------------------------------------
     * 7. TIM2 — interrupt every 20000 cycles (UIE via DIER)
     * ------------------------------------------------------- */
    TIM2_PSC  = 0U;
    TIM2_ARR  = 19999U;
    TIM2_SR   = 0U;
    TIM2_DIER = TIM2_DIER_UIE;   /* enable update interrupt */
    TIM2_CR1  = TIM2_CR1_CEN;

    uart_puts("[TIM2]    ARR=19999 PSC=0 UIE=1 CEN=1 (interrupt every 20000 cycles)\r\n");
    uart_puts("\r\nWaiting for interrupts...\r\n\r\n");

    /* -------------------------------------------------------
     * 8. Main loop — spin until both ISRs have fired enough
     *    (global interrupts are always enabled in the emulator)
     * ------------------------------------------------------- */
    while (systick_cnt < 4 || tim2_cnt < 2)
        ;

    /* -------------------------------------------------------
     * 9. Summary
     * ------------------------------------------------------- */
    uart_puts("\r\n[DONE] All peripherals verified:\r\n");
    uart_puts("       RCC     — HSI ready, PLL locked, PLL as SYSCLK\r\n");
    uart_puts("       GPIOA   — PA5 toggled via SysTick ISR (BSRR)\r\n");
    uart_puts("       GPIOB   — PB0 toggled via TIM2 ISR (BSRR)\r\n");
    uart_puts("       NVIC    — IRQ28 enabled, priority set via IPR7\r\n");
    uart_puts("       SysTick — ");
    uart_putdec((uint32_t)systick_cnt);
    uart_puts(" interrupts fired (TICKINT mode)\r\n");
    uart_puts("       TIM2    — ");
    uart_putdec((uint32_t)tim2_cnt);
    uart_puts(" interrupts fired (UIE+DIER mode)\r\n");
    uart_puts("       USART1  — this output (from main + ISR contexts)\r\n");

    while (1)
        ;
}
