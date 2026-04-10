#ifndef STM32_GPIO_H
#define STM32_GPIO_H

#include <stdint.h>
#include "common/status.h"
#include "peripherals/peripheral.h"

/*
 * GPIO — General Purpose I/O (STM32F103).
 * One GpioState represents a single port (A, B, C, ...).
 *
 * Register map (offsets from port base):
 *   0x00  CRL   — Configuration register low  (pins 0–7,  4 bits each)
 *   0x04  CRH   — Configuration register high (pins 8–15, 4 bits each)
 *   0x08  IDR   — Input  data register (read-only, 16 lower bits)
 *   0x0C  ODR   — Output data register (16 lower bits)
 *   0x10  BSRR  — Bit set/reset register (write-only)
 *                   bits 15:0  = set   bits in ODR
 *                   bits 31:16 = reset bits in ODR
 *   0x14  BRR   — Bit reset register (write-only, bits 15:0 reset ODR)
 *   0x18  LCKR  — Lock register (stub: read/write, no lock logic)
 *
 * Pin mode (from CRL/CRH, bits [1:0] of each 4-bit group):
 *   00 = Input mode
 *   01/10/11 = Output mode (speed differs, but we ignore speed)
 *
 * IDR is computed on each read:
 *   output pin → IDR bit = ODR bit
 *   input  pin → IDR bit = ext_input bit (driven externally via gpio_set_pin)
 */

#define GPIO_SIZE  0x400U

/* Register offsets */
#define GPIO_CRL_OFFSET   0x00
#define GPIO_CRH_OFFSET   0x04
#define GPIO_IDR_OFFSET   0x08
#define GPIO_ODR_OFFSET   0x0C
#define GPIO_BSRR_OFFSET  0x10
#define GPIO_BRR_OFFSET   0x14
#define GPIO_LCKR_OFFSET  0x18

/* Port base addresses */
#define GPIOA_BASE  0x40010800U
#define GPIOB_BASE  0x40010C00U
#define GPIOC_BASE  0x40011000U
#define GPIOD_BASE  0x40011400U
#define GPIOE_BASE  0x40011800U

typedef struct {
    uint32_t crl;        /* Pin config: pins 0–7  */
    uint32_t crh;        /* Pin config: pins 8–15 */
    uint16_t odr;        /* Output data (written by firmware) */
    uint16_t ext_input;  /* External input state (set by test/simulation) */
    uint32_t lckr;       /* Lock register (stub) */
} GpioState;

void gpio_init(GpioState* gpio);

Peripheral gpio_as_peripheral(GpioState* gpio);

/**
 * Drive an input pin from outside (e.g. simulating a button press).
 * Only affects pins configured as input; ignored for output pins.
 */
void gpio_set_pin(GpioState* gpio, uint8_t pin, int level);

/** Read the current logical level of a pin (accounts for direction). */
int  gpio_get_pin(const GpioState* gpio, uint8_t pin);

/* Peripheral callbacks */
uint32_t gpio_read(void* ctx, uint32_t offset, uint8_t size);
Status   gpio_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size);
void     gpio_reset(void* ctx);

#endif /* STM32_GPIO_H */
