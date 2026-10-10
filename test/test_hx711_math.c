/*
 * Host-side check of the sign extension and the weight conversion.
 *
 * Two numbers turn counts into weight and both are measured, not derived --
 * so the guards here are mostly about refusing calibrations that would make
 * every later reading silently wrong.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../src/hx711/hx711_math.h"

#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-4 * fabs(b) + 1e-9)

static void test_sign_extension(void)
{
    assert(hx711_sext24(0x000000) == 0);
    assert(hx711_sext24(0x000001) == 1);
    assert(hx711_sext24(0x7FFFFF) == 8388607);
    assert(hx711_sext24(0x800000) == -8388608);
    assert(hx711_sext24(0xFFFFFF) == -1);
    assert(hx711_sext24(0xFFFFFE) == -2);
    /* Anything above 24 bits is not ours and must be ignored. */
    assert(hx711_sext24(0xFF000001) == 1);
    assert(hx711_sext24(0xAB800000) == -8388608);

    assert(hx711_sext24(0x7FFFFF) == HX711_COUNT_MAX);
    assert(hx711_sext24(0x800000) == HX711_COUNT_MIN);
}

static void test_gain_table(void)
{
    /* From Linux's hx711_gain_to_scale[]: 128->1, 32->2, 64->3 pulses. */
    assert(hx711_gain_pulses(128) == 1);
    assert(hx711_gain_pulses(32) == 2);
    assert(hx711_gain_pulses(64) == 3);
    /* The pulse count is NOT ordered by gain -- guard that it stays this way. */
    assert(hx711_gain_pulses(64) > hx711_gain_pulses(32));

    assert(hx711_gain_pulses(0) == 0);
    assert(hx711_gain_pulses(1) == 0);
    assert(hx711_gain_pulses(256) == 0);
    assert(hx711_gain_pulses(-128) == 0);

    /* Gain 32 is the only one on channel B. */
    assert(hx711_gain_channel(128) == 'A');
    assert(hx711_gain_channel(64) == 'A');
    assert(hx711_gain_channel(32) == 'B');
    assert(hx711_gain_channel(16) == '?');

    /* A power cycle leaves it at 128. */
    assert(HX711_RESET_GAIN == 128);
    assert(hx711_gain_pulses(HX711_RESET_GAIN) == 1);
}

static void test_units(void)
{
    /* (raw - offset) / scale */
    assert(NEAR(hx711_to_units(2000, 1000, 100.0f), 10.0f));
    assert(NEAR(hx711_to_units(1000, 1000, 100.0f), 0.0f));
    assert(NEAR(hx711_to_units(0, 1000, 100.0f), -10.0f));
    /* A negative scale just means the cell is wired the other way up. */
    assert(NEAR(hx711_to_units(2000, 1000, -100.0f), -10.0f));

    /* Uncalibrated must give 0, not infinity. */
    assert(hx711_to_units(2000, 1000, 0.0f) == 0.0f);

    /* Offsets near the ends of the range must not overflow the arithmetic. */
    assert(NEAR(hx711_to_units(HX711_COUNT_MAX, HX711_COUNT_MIN, 1.0f),
                (float)HX711_COUNT_MAX - (float)HX711_COUNT_MIN));
}

static void test_scale_solver(void)
{
    float s = 0.0f;

    /* 1000 counts for 2 units is 500 counts per unit. */
    assert(hx711_solve_scale(3000, 2000, 2.0f, &s) && NEAR(s, 500.0f));
    /* Round trip: the solved scale must read the known weight back. */
    assert(NEAR(hx711_to_units(3000, 2000, s), 2.0f));

    /* A cell wired the other way gives a negative scale, which is fine. */
    assert(hx711_solve_scale(1000, 2000, 2.0f, &s) && NEAR(s, -500.0f));
    assert(NEAR(hx711_to_units(1000, 2000, s), 2.0f));

    /* Nonsense inputs must be refused, not turned into an absurd scale. */
    assert(!hx711_solve_scale(3000, 2000, 0.0f, &s));
    assert(!hx711_solve_scale(3000, 2000, -1.0f, &s));
    /* Barely any movement: the mass was never put on, or nothing is wired.
     * A load cell reads in millions of counts, so a delta this small is noise
     * and the scale from it would be noise too. */
    assert(!hx711_solve_scale(2000, 2000, 2.0f, &s));
    assert(!hx711_solve_scale(2000, 1999, 2.0f, &s));
    assert(!hx711_solve_scale(2000, 2000 - 99, 2.0f, &s));
    /* At the floor it is accepted again. */
    assert(hx711_solve_scale(2000 + 100, 2000, 2.0f, &s) && NEAR(s, 50.0f));
}

static void test_saturation(void)
{
    assert(hx711_saturated(HX711_COUNT_MAX));
    assert(hx711_saturated(HX711_COUNT_MIN));
    assert(hx711_saturated(HX711_COUNT_MAX - 1));
    assert(hx711_saturated(HX711_COUNT_MIN + 1));
    assert(!hx711_saturated(0));
    assert(!hx711_saturated(HX711_COUNT_MAX - 2));
    assert(!hx711_saturated(HX711_COUNT_MIN + 2));
}

int main(void)
{
    test_sign_extension();
    test_gain_table();
    test_units();
    test_scale_solver();
    test_saturation();
    printf("all hx711 math checks passed\n");
    return 0;
}
