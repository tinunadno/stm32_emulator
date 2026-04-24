#include "tests/test.h"
#include "peripherals/gpio/gpio.h"

static uint32_t r(GpioState* g, uint32_t off) { return gpio_read(g, off, 4); }
static void     w(GpioState* g, uint32_t off, uint32_t v) { gpio_write(g, off, v, 4); }

/* --- Test: reset state — all pins input floating (CRL/CRH = 0x44444444) --- */
static void test_gpio_reset_state(void)
{
    GpioState gpio;
    gpio_init(&gpio);

    ASSERT_EQ(r(&gpio, GPIO_CRL_OFFSET), 0x44444444U);
    ASSERT_EQ(r(&gpio, GPIO_CRH_OFFSET), 0x44444444U);
    ASSERT_EQ(r(&gpio, GPIO_ODR_OFFSET), 0x0000U);
    ASSERT_EQ(r(&gpio, GPIO_IDR_OFFSET), 0x0000U);
}

/* --- Test: configure pin as output, write ODR, IDR reflects ODR --- */
static void test_gpio_output_idr_tracks_odr(void)
{
    GpioState gpio;
    gpio_init(&gpio);

    /* Set pin 5 as output 2MHz push-pull: MODE=10, CNF=00 → 0b0010 = 0x2
     * CRL bits 23:20 = 0x2 for pin 5 */
    uint32_t crl = 0x44444444U;
    crl &= ~(0xFU << 20);
    crl |=  (0x2U << 20);  /* Pin 5: output 2 MHz */
    w(&gpio, GPIO_CRL_OFFSET, crl);

    /* Write ODR: set pin 5 high */
    w(&gpio, GPIO_ODR_OFFSET, 1U << 5);
    ASSERT(r(&gpio, GPIO_IDR_OFFSET) & (1U << 5));

    /* Clear pin 5 */
    w(&gpio, GPIO_ODR_OFFSET, 0);
    ASSERT_EQ(r(&gpio, GPIO_IDR_OFFSET) & (1U << 5), 0);
}

/* --- Test: BSRR set/reset --- */
static void test_gpio_bsrr(void)
{
    GpioState gpio;
    gpio_init(&gpio);

    /* Configure pins 0 and 3 as outputs */
    uint32_t crl = 0x44444444U;
    crl &= ~(0xFU << 0);   crl |= 0x2U;          /* Pin 0 output */
    crl &= ~(0xFU << 12);  crl |= (0x2U << 12);  /* Pin 3 output */
    w(&gpio, GPIO_CRL_OFFSET, crl);

    /* Set pin 0 and pin 3 via BSRR low half */
    w(&gpio, GPIO_BSRR_OFFSET, (1U << 0) | (1U << 3));
    ASSERT(r(&gpio, GPIO_ODR_OFFSET) & (1U << 0));
    ASSERT(r(&gpio, GPIO_ODR_OFFSET) & (1U << 3));

    /* Reset pin 0 via BSRR high half */
    w(&gpio, GPIO_BSRR_OFFSET, 1U << 16);
    ASSERT_EQ(r(&gpio, GPIO_ODR_OFFSET) & (1U << 0), 0);
    ASSERT(r(&gpio, GPIO_ODR_OFFSET) & (1U << 3));   /* Pin 3 still set */
}

/* --- Test: BRR clears bits --- */
static void test_gpio_brr(void)
{
    GpioState gpio;
    gpio_init(&gpio);

    /* Configure pin 7 as output */
    uint32_t crl = 0x44444444U;
    crl &= ~(0xFU << 28);
    crl |=  (0x2U << 28);
    w(&gpio, GPIO_CRL_OFFSET, crl);

    w(&gpio, GPIO_ODR_OFFSET, 1U << 7);
    ASSERT(r(&gpio, GPIO_ODR_OFFSET) & (1U << 7));

    w(&gpio, GPIO_BRR_OFFSET, 1U << 7);
    ASSERT_EQ(r(&gpio, GPIO_ODR_OFFSET) & (1U << 7), 0);
}

/* --- Test: input pin driven by gpio_set_pin --- */
static void test_gpio_input_ext(void)
{
    GpioState gpio;
    gpio_init(&gpio);

    /* All pins input (reset state), drive pin 4 high externally */
    gpio_set_pin(&gpio, 4, 1);
    ASSERT(r(&gpio, GPIO_IDR_OFFSET) & (1U << 4));

    gpio_set_pin(&gpio, 4, 0);
    ASSERT_EQ(r(&gpio, GPIO_IDR_OFFSET) & (1U << 4), 0);
}

/* --- Test: gpio_get_pin returns correct level --- */
static void test_gpio_get_pin(void)
{
    GpioState gpio;
    gpio_init(&gpio);

    /* Configure pin 2 as output, set it high */
    uint32_t crl = 0x44444444U;
    crl &= ~(0xFU << 8);
    crl |=  (0x2U << 8);
    w(&gpio, GPIO_CRL_OFFSET, crl);
    w(&gpio, GPIO_ODR_OFFSET, 1U << 2);

    ASSERT_EQ(gpio_get_pin(&gpio, 2), 1);
    w(&gpio, GPIO_ODR_OFFSET, 0);
    ASSERT_EQ(gpio_get_pin(&gpio, 2), 0);

    /* Input pin driven externally */
    gpio_set_pin(&gpio, 9, 1);
    ASSERT_EQ(gpio_get_pin(&gpio, 9), 1);
}

/* --- Test: BSRR reset takes precedence over set (same bit) --- */
static void test_gpio_bsrr_reset_priority(void)
{
    GpioState gpio;
    gpio_init(&gpio);

    /* Pin 1 output */
    uint32_t crl = 0x44444444U;
    crl &= ~(0xFU << 4);
    crl |=  (0x2U << 4);
    w(&gpio, GPIO_CRL_OFFSET, crl);

    w(&gpio, GPIO_ODR_OFFSET, 0);
    /* Set and reset pin 1 simultaneously → reset wins */
    w(&gpio, GPIO_BSRR_OFFSET, (1U << 17) | (1U << 1));
    ASSERT_EQ(r(&gpio, GPIO_ODR_OFFSET) & (1U << 1), 0);
}

void test_gpio_all(void)
{
    TEST_SUITE("GPIO");
    RUN_TEST(test_gpio_reset_state);
    RUN_TEST(test_gpio_output_idr_tracks_odr);
    RUN_TEST(test_gpio_bsrr);
    RUN_TEST(test_gpio_brr);
    RUN_TEST(test_gpio_input_ext);
    RUN_TEST(test_gpio_get_pin);
    RUN_TEST(test_gpio_bsrr_reset_priority);
}
