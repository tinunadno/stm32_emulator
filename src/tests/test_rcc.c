#include "tests/test.h"
#include "peripherals/rcc/rcc.h"

/* Helper: read/write via peripheral callbacks */
static uint32_t r(RccState* rcc, uint32_t offset)
{
    return rcc_read(rcc, offset, 4);
}

static void w(RccState* rcc, uint32_t offset, uint32_t value)
{
    rcc_write(rcc, offset, value, 4);
}

/* --- Test: reset values --- */
static void test_rcc_reset_values(void)
{
    RccState rcc;
    rcc_init(&rcc);

    /* HSI on and ready at reset */
    ASSERT(r(&rcc, RCC_CR_OFFSET) & RCC_CR_HSION);
    ASSERT(r(&rcc, RCC_CR_OFFSET) & RCC_CR_HSIRDY);

    /* PLL and HSE off at reset */
    ASSERT_EQ(r(&rcc, RCC_CR_OFFSET) & RCC_CR_HSEON,  0);
    ASSERT_EQ(r(&rcc, RCC_CR_OFFSET) & RCC_CR_PLLON,  0);
    ASSERT_EQ(r(&rcc, RCC_CR_OFFSET) & RCC_CR_PLLRDY, 0);

    /* Reset cause flags set */
    ASSERT(r(&rcc, RCC_CSR_OFFSET) & RCC_CSR_RESET_FLAGS);
}

/* --- Test: HSIRDY follows HSION immediately --- */
static void test_rcc_hsi_ready(void)
{
    RccState rcc;
    rcc_init(&rcc);

    /* Disable HSI */
    w(&rcc, RCC_CR_OFFSET, 0);
    ASSERT_EQ(r(&rcc, RCC_CR_OFFSET) & RCC_CR_HSIRDY, 0);

    /* Re-enable HSI → HSIRDY immediately set */
    w(&rcc, RCC_CR_OFFSET, RCC_CR_HSION);
    ASSERT(r(&rcc, RCC_CR_OFFSET) & RCC_CR_HSIRDY);
}

/* --- Test: HSERDY follows HSEON, PLLRDY follows PLLON --- */
static void test_rcc_hse_pll_ready(void)
{
    RccState rcc;
    rcc_init(&rcc);

    /* Enable HSE + PLL */
    w(&rcc, RCC_CR_OFFSET, RCC_CR_HSION | RCC_CR_HSEON | RCC_CR_PLLON);

    uint32_t cr = r(&rcc, RCC_CR_OFFSET);
    ASSERT(cr & RCC_CR_HSIRDY);
    ASSERT(cr & RCC_CR_HSERDY);
    ASSERT(cr & RCC_CR_PLLRDY);

    /* Disable HSE and PLL */
    w(&rcc, RCC_CR_OFFSET, RCC_CR_HSION);
    cr = r(&rcc, RCC_CR_OFFSET);
    ASSERT_EQ(cr & RCC_CR_HSERDY, 0);
    ASSERT_EQ(cr & RCC_CR_PLLRDY, 0);
}

/* --- Test: SWS follows SW in CFGR (no spin-wait needed) --- */
static void test_rcc_cfgr_sws_tracks_sw(void)
{
    RccState rcc;
    rcc_init(&rcc);

    /* SW = 0 (HSI) → SWS = 0 */
    w(&rcc, RCC_CFGR_OFFSET, 0x00000000U);
    ASSERT_EQ((r(&rcc, RCC_CFGR_OFFSET) & RCC_CFGR_SWS_MASK) >> RCC_CFGR_SWS_SHIFT, 0);

    /* SW = 1 (HSE) → SWS = 1 */
    w(&rcc, RCC_CFGR_OFFSET, 0x00000001U);
    ASSERT_EQ((r(&rcc, RCC_CFGR_OFFSET) & RCC_CFGR_SWS_MASK) >> RCC_CFGR_SWS_SHIFT, 1);

    /* SW = 2 (PLL) → SWS = 2 */
    w(&rcc, RCC_CFGR_OFFSET, 0x00000002U);
    ASSERT_EQ((r(&rcc, RCC_CFGR_OFFSET) & RCC_CFGR_SWS_MASK) >> RCC_CFGR_SWS_SHIFT, 2);
}

/* --- Test: APB2ENR/APB1ENR are plain read/write --- */
static void test_rcc_enr_readwrite(void)
{
    RccState rcc;
    rcc_init(&rcc);

    /* Enable GPIOA clock (bit 2 in APB2ENR) */
    w(&rcc, 0x18, 1U << 2);
    ASSERT(r(&rcc, 0x18) & (1U << 2));

    /* Enable TIM2 clock (bit 0 in APB1ENR) */
    w(&rcc, 0x1C, 1U << 0);
    ASSERT(r(&rcc, 0x1C) & (1U << 0));
}

/* --- Test: CSR RMVF clears reset flags --- */
static void test_rcc_csr_rmvf(void)
{
    RccState rcc;
    rcc_init(&rcc);

    ASSERT(r(&rcc, RCC_CSR_OFFSET) & RCC_CSR_RESET_FLAGS);

    /* Write RMVF bit (bit 24) to clear reset flags */
    w(&rcc, RCC_CSR_OFFSET, 1U << 24);
    ASSERT_EQ(r(&rcc, RCC_CSR_OFFSET) & 0xFF000000U, 0);
}

/* --- Test: rcc_reset restores defaults --- */
static void test_rcc_peripheral_reset(void)
{
    RccState rcc;
    rcc_init(&rcc);

    /* Clobber some registers */
    w(&rcc, RCC_CFGR_OFFSET, 0xDEADBEEFU);
    w(&rcc, 0x18, 0xFFFFFFFFU);

    rcc_reset(&rcc);

    ASSERT(r(&rcc, RCC_CR_OFFSET) & RCC_CR_HSIRDY);
    ASSERT_EQ(r(&rcc, RCC_CFGR_OFFSET), 0x00000000U);
    ASSERT_EQ(r(&rcc, 0x18), 0x00000000U);
}

void test_rcc_all(void)
{
    TEST_SUITE("RCC");
    RUN_TEST(test_rcc_reset_values);
    RUN_TEST(test_rcc_hsi_ready);
    RUN_TEST(test_rcc_hse_pll_ready);
    RUN_TEST(test_rcc_cfgr_sws_tracks_sw);
    RUN_TEST(test_rcc_enr_readwrite);
    RUN_TEST(test_rcc_csr_rmvf);
    RUN_TEST(test_rcc_peripheral_reset);
}
