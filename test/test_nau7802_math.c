/*
 * Host-side check of the conversions. Every expected value below is taken
 * from Nuvoton's datasheet revision 1.7, section 11.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/nau7802/nau7802_math.h"
#include "../src/nau7802/nau7802_regs.h"

static void test_sign_extension(void)
{
    assert(nau7802_sext24(0x000000) == 0);
    assert(nau7802_sext24(0x7FFFFF) == 8388607);
    assert(nau7802_sext24(0x800000) == -8388608);
    assert(nau7802_sext24(0xFFFFFF) == -1);
    /* Anything above 24 bits is not ours. */
    assert(nau7802_sext24(0xFF000001) == 1);
    assert(nau7802_sext24(0x7FFFFF) == NAU7802_COUNT_MAX);
    assert(nau7802_sext24(0x800000) == NAU7802_COUNT_MIN);
}

static void test_raw_is_big_endian(void)
{
    const uint8_t pos[3] = { 0x00, 0x00, 0x01 };
    const uint8_t neg[3] = { 0xFF, 0xFF, 0xFF };
    const uint8_t hi[3]  = { 0x12, 0x34, 0x56 };

    /* ADCO_B2 is the most significant byte, at the lowest address. */
    assert(nau7802_raw(pos) == 1);
    assert(nau7802_raw(neg) == -1);
    assert(nau7802_raw(hi) == 0x123456);
}

static void test_gain_table(void)
{
    /* Section 11.2: 000 = x1, each step doubles, 111 = x128. */
    assert(nau7802_gain_value(0) == 1);
    assert(nau7802_gain_value(1) == 2);
    assert(nau7802_gain_value(3) == 8);
    assert(nau7802_gain_value(7) == 128);
    for (int g = 0; g < 7; g++)
        assert(nau7802_gain_value(g + 1) == nau7802_gain_value(g) * 2);
    assert(nau7802_gain_value(8) == 0);
    assert(nau7802_gain_value(-1) == 0);
}

static void test_rate_table_has_a_gap(void)
{
    /* Section 11.3: 000..011 are 10/20/40/80 SPS, then 320 SPS is code 111.
     * Codes 4, 5 and 6 are not defined -- treating the code as a dense index
     * silently puts you at the wrong rate. */
    assert(nau7802_rate_sps(0) == 10);
    assert(nau7802_rate_sps(1) == 20);
    assert(nau7802_rate_sps(2) == 40);
    assert(nau7802_rate_sps(3) == 80);
    assert(nau7802_rate_sps(7) == 320);

    assert(nau7802_rate_sps(4) == 0);
    assert(nau7802_rate_sps(5) == 0);
    assert(nau7802_rate_sps(6) == 0);
    assert(nau7802_rate_sps(8) == 0);
    assert(nau7802_rate_sps(-1) == 0);

    assert(nau7802_conv_time_ms(0) >= 100);   /* 10 SPS */
    assert(nau7802_conv_time_ms(7) >= 3);     /* 320 SPS */
    assert(nau7802_conv_time_ms(0) > nau7802_conv_time_ms(7));
    assert(nau7802_conv_time_ms(4) == 0);
}

static void test_ldo_table_runs_backwards(void)
{
    /* Section 11.2: 000 is the HIGHEST voltage. */
    assert(nau7802_ldo_mv(0) == 4500);
    assert(nau7802_ldo_mv(4) == 3300);
    assert(nau7802_ldo_mv(7) == 2400);
    assert(nau7802_ldo_mv(0) > nau7802_ldo_mv(7));
    /* Every step down is 300 mV. */
    for (int c = 0; c < 7; c++)
        assert(nau7802_ldo_mv(c) - nau7802_ldo_mv(c + 1) == 300);
    assert(nau7802_ldo_mv(8) == 0);
}

static void test_units_and_scale(void)
{
    float s = 0.0f;

    assert(fabs(nau7802_to_units(2000, 1000, 100.0f) - 10.0f) < 1e-4);
    assert(nau7802_to_units(1000, 1000, 100.0f) == 0.0f);
    /* A cell wired the other way gives a negative scale; that is fine. */
    assert(fabs(nau7802_to_units(2000, 1000, -100.0f) + 10.0f) < 1e-4);
    /* Uncalibrated returns 0, not infinity. */
    assert(nau7802_to_units(2000, 1000, 0.0f) == 0.0f);

    assert(nau7802_solve_scale(3000, 2000, 2.0f, &s) && fabs(s - 500.0f) < 1e-3);
    assert(fabs(nau7802_to_units(3000, 2000, s) - 2.0f) < 1e-4);

    /* Barely any movement means the mass was never on the cell. */
    assert(!nau7802_solve_scale(2000, 2000, 2.0f, &s));
    assert(!nau7802_solve_scale(2000 + 99, 2000, 2.0f, &s));
    assert(nau7802_solve_scale(2000 + 100, 2000, 2.0f, &s));
    assert(!nau7802_solve_scale(3000, 2000, 0.0f, &s));
    assert(!nau7802_solve_scale(3000, 2000, -1.0f, &s));
}

static void test_saturation(void)
{
    assert(nau7802_saturated(NAU7802_COUNT_MAX));
    assert(nau7802_saturated(NAU7802_COUNT_MIN));
    assert(!nau7802_saturated(0));
    assert(!nau7802_saturated(NAU7802_COUNT_MAX - 2));
}

static void test_register_constants(void)
{
    /* Section 10, summary register map. */
    assert(NAU7802_ADDR == 0x2A);
    assert(NAU7802_PU_CTRL == 0x00 && NAU7802_CTRL1 == 0x01);
    assert(NAU7802_CTRL2 == 0x02 && NAU7802_ADCO_B2 == 0x12);
    assert(NAU7802_REVISION == 0x1F);
    /* Section 11.1 bit positions. */
    assert(NAU7802_RR == 0x01 && NAU7802_PUD == 0x02);
    assert(NAU7802_PUA == 0x04 && NAU7802_PUR == 0x08);
    assert(NAU7802_CS == 0x10 && NAU7802_CR == 0x20);
    assert(NAU7802_AVDDS == 0x80);
    /* Section 11.3. */
    assert(NAU7802_CALS == 0x04 && NAU7802_CAL_ERR == 0x08);
    assert(NAU7802_CHS == 0x80 && NAU7802_CRS_SHIFT == 4);
    /* The gain calibration reset default, 0x00800000, is unity. */
    assert(NAU7802_GCAL_UNITY == 0x00800000UL);
}

int main(void)
{
    test_sign_extension();
    test_raw_is_big_endian();
    test_gain_table();
    test_rate_table_has_a_gap();
    test_ldo_table_runs_backwards();
    test_units_and_scale();
    test_saturation();
    test_register_constants();
    printf("all nau7802 math checks passed\n");
    return 0;
}
