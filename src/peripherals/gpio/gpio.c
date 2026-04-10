#include "peripherals/gpio/gpio.h"
#include <string.h>
#include <stdio.h>

/* ---- helpers ---- */

/*
 * Build a 16-bit mask of pins currently in output mode.
 * CRL covers pins 0–7, CRH covers pins 8–15.
 * Each pin occupies 4 bits; bits [1:0] = MODE (0 = input, non-zero = output).
 */
static uint16_t output_mask(const GpioState* gpio)
{
    uint16_t mask = 0;

    for (int i = 0; i < 8; i++) {
        if ((gpio->crl >> (i * 4)) & 0x3U)
            mask |= (uint16_t)(1U << i);
    }
    for (int i = 0; i < 8; i++) {
        if ((gpio->crh >> (i * 4)) & 0x3U)
            mask |= (uint16_t)(1U << (i + 8));
    }
    return mask;
}

/* IDR: output pins reflect ODR, input pins reflect ext_input. */
static uint16_t compute_idr(const GpioState* gpio)
{
    uint16_t out = output_mask(gpio);
    return (uint16_t)((out & gpio->odr) | (~out & gpio->ext_input));
}

/* ---- public API ---- */

void gpio_init(GpioState* gpio)
{
    memset(gpio, 0, sizeof(GpioState));
    /* Reset: all pins in input floating mode (CNF=01, MODE=00 → 0x44444444) */
    gpio->crl = 0x44444444U;
    gpio->crh = 0x44444444U;
}

Peripheral gpio_as_peripheral(GpioState* gpio)
{
    Peripheral p;
    p.ctx   = gpio;
    p.read  = gpio_read;
    p.write = gpio_write;
    p.tick  = NULL;
    p.reset = gpio_reset;
    return p;
}

void gpio_reset(void* ctx)
{
    gpio_init((GpioState*)ctx);
}

void gpio_set_pin(GpioState* gpio, uint8_t pin, int level)
{
    if (pin >= 16) return;
    if (level)
        gpio->ext_input |= (uint16_t)(1U << pin);
    else
        gpio->ext_input &= (uint16_t)~(1U << pin);
}

int gpio_get_pin(const GpioState* gpio, uint8_t pin)
{
    if (pin >= 16) return 0;
    return (compute_idr(gpio) >> pin) & 1;
}

uint32_t gpio_read(void* ctx, uint32_t offset, uint8_t size)
{
    GpioState* gpio = (GpioState*)ctx;
    (void)size;

    switch (offset) {
    case GPIO_CRL_OFFSET:  return gpio->crl;
    case GPIO_CRH_OFFSET:  return gpio->crh;
    case GPIO_IDR_OFFSET:  return compute_idr(gpio) & 0xFFFFU;
    case GPIO_ODR_OFFSET:  return gpio->odr & 0xFFFFU;
    case GPIO_BSRR_OFFSET: return 0;  /* Write-only */
    case GPIO_BRR_OFFSET:  return 0;  /* Write-only */
    case GPIO_LCKR_OFFSET: return gpio->lckr;
    default:
        fprintf(stderr, "GPIO: read from unknown offset 0x%02X\n", offset);
        return 0;
    }
}

Status gpio_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size)
{
    GpioState* gpio = (GpioState*)ctx;
    (void)size;

    switch (offset) {
    case GPIO_CRL_OFFSET:
        gpio->crl = value;
        break;

    case GPIO_CRH_OFFSET:
        gpio->crh = value;
        break;

    case GPIO_ODR_OFFSET:
        gpio->odr = (uint16_t)(value & 0xFFFFU);
        break;

    case GPIO_BSRR_OFFSET: {
        /* High 16 bits reset, low 16 bits set — reset takes precedence on conflict */
        uint16_t set_bits   = (uint16_t)(value & 0xFFFFU);
        uint16_t reset_bits = (uint16_t)((value >> 16) & 0xFFFFU);
        gpio->odr |=  set_bits;
        gpio->odr &= ~reset_bits;
        break;
    }

    case GPIO_BRR_OFFSET:
        gpio->odr &= ~(uint16_t)(value & 0xFFFFU);
        break;

    case GPIO_LCKR_OFFSET:
        gpio->lckr = value;
        break;

    default:
        fprintf(stderr, "GPIO: write to unknown offset 0x%02X\n", offset);
        return STATUS_ERROR;
    }
    return STATUS_OK;
}
