#include <stdint.h>

extern int main(void);

/* Defined by the linker script */
extern uint32_t _estack;
extern uint32_t _sbss;
extern uint32_t _ebss;

void Reset_Handler(void)
{
    /* Zero out .bss section */
    for (uint32_t* p = &_sbss; p < &_ebss; p++)
        *p = 0;

    main();

    /* If main returns, hang */
    while (1)
        ;
}

void Default_Handler(void)
{
    while (1)
        ;
}

/*
 * Weak interrupt handlers — override in application code by defining
 * a function with the same name (no 'weak' attribute needed).
 */
void __attribute__((weak, alias("Default_Handler"))) NMI_Handler(void);
void __attribute__((weak, alias("Default_Handler"))) HardFault_Handler(void);
void __attribute__((weak, alias("Default_Handler"))) SysTick_Handler(void);
void __attribute__((weak, alias("Default_Handler"))) TIM2_IRQHandler(void);

/*
 * Vector table — placed at the very start of Flash (0x08000000).
 *
 * Cortex-M3 layout:
 *   [0]  = Initial stack pointer
 *   [1]  = Reset handler
 *   [15] = SysTick (exception 15)
 *   [44] = IRQ28 = TIM2
 */
__attribute__((section(".isr_vector"), used))
const uint32_t vector_table[] = {
    (uint32_t)&_estack,           /*  0 : Initial SP            */
    (uint32_t)&Reset_Handler,     /*  1 : Reset                 */
    (uint32_t)&NMI_Handler,       /*  2 : NMI                   */
    (uint32_t)&HardFault_Handler, /*  3 : HardFault             */
    (uint32_t)&Default_Handler,   /*  4 : MemManage             */
    (uint32_t)&Default_Handler,   /*  5 : BusFault              */
    (uint32_t)&Default_Handler,   /*  6 : UsageFault            */
    0, 0, 0, 0,                   /*  7-10 : Reserved           */
    (uint32_t)&Default_Handler,   /* 11 : SVCall                */
    0, 0,                         /* 12-13 : Reserved           */
    (uint32_t)&Default_Handler,   /* 14 : PendSV                */
    (uint32_t)&SysTick_Handler,   /* 15 : SysTick               */
    /* IRQ 0..42 */
    (uint32_t)&Default_Handler,   /* 16 : IRQ0  - WWDG          */
    (uint32_t)&Default_Handler,   /* 17 : IRQ1  - PVD           */
    (uint32_t)&Default_Handler,   /* 18 : IRQ2  - TAMPER        */
    (uint32_t)&Default_Handler,   /* 19 : IRQ3  - RTC           */
    (uint32_t)&Default_Handler,   /* 20 : IRQ4  - FLASH         */
    (uint32_t)&Default_Handler,   /* 21 : IRQ5  - RCC           */
    (uint32_t)&Default_Handler,   /* 22 : IRQ6  - EXTI0         */
    (uint32_t)&Default_Handler,   /* 23 : IRQ7  - EXTI1         */
    (uint32_t)&Default_Handler,   /* 24 : IRQ8  - EXTI2         */
    (uint32_t)&Default_Handler,   /* 25 : IRQ9  - EXTI3         */
    (uint32_t)&Default_Handler,   /* 26 : IRQ10 - EXTI4         */
    (uint32_t)&Default_Handler,   /* 27 : IRQ11 - DMA1_Ch1      */
    (uint32_t)&Default_Handler,   /* 28 : IRQ12 - DMA1_Ch2      */
    (uint32_t)&Default_Handler,   /* 29 : IRQ13 - DMA1_Ch3      */
    (uint32_t)&Default_Handler,   /* 30 : IRQ14 - DMA1_Ch4      */
    (uint32_t)&Default_Handler,   /* 31 : IRQ15 - DMA1_Ch5      */
    (uint32_t)&Default_Handler,   /* 32 : IRQ16 - DMA1_Ch6      */
    (uint32_t)&Default_Handler,   /* 33 : IRQ17 - DMA1_Ch7      */
    (uint32_t)&Default_Handler,   /* 34 : IRQ18 - ADC1_2        */
    (uint32_t)&Default_Handler,   /* 35 : IRQ19 - USB_HP        */
    (uint32_t)&Default_Handler,   /* 36 : IRQ20 - USB_LP        */
    (uint32_t)&Default_Handler,   /* 37 : IRQ21 - CAN_RX1       */
    (uint32_t)&Default_Handler,   /* 38 : IRQ22 - CAN_SCE       */
    (uint32_t)&Default_Handler,   /* 39 : IRQ23 - EXTI9_5       */
    (uint32_t)&Default_Handler,   /* 40 : IRQ24 - TIM1_BRK      */
    (uint32_t)&Default_Handler,   /* 41 : IRQ25 - TIM1_UP       */
    (uint32_t)&Default_Handler,   /* 42 : IRQ26 - TIM1_TRG_COM  */
    (uint32_t)&Default_Handler,   /* 43 : IRQ27 - TIM1_CC       */
    (uint32_t)&TIM2_IRQHandler,   /* 44 : IRQ28 - TIM2          */
};
