/*
 * Sign extension, the gain table and the weight conversion, split out so
 * they can be compiled and asserted on the host. The calibration arithmetic
 * is the one both chips share, in loadcell_math.h; these are its HX711
 * names, which esp-hx711-loadcell's own tests use.
 */
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "loadcell_math.h"
#include "hx711_proto.h"

/* The result is 24-bit two's complement in a 32-bit word. */
static inline int32_t hx711_sext24(uint32_t v)
{
    return loadcell_sext24(v);
}

/* Full scale of the 24-bit converter, for range checks (Table 2, p.3). */
#define HX711_COUNT_MIN  LOADCELL_COUNT_MIN
#define HX711_COUNT_MAX  LOADCELL_COUNT_MAX

/*
 * Trailing pulses for a gain. Returns 0 for a gain the part cannot do, which
 * the caller must treat as an error rather than clocking out.
 */
static inline int hx711_gain_pulses(int gain)
{
    if (gain == 128)
        return HX711_PULSES_A_128;
    if (gain == 32)
        return HX711_PULSES_B_32;
    if (gain == 64)
        return HX711_PULSES_A_64;
    return 0;
}

/* Which input a gain selects. 32 is the only one on channel B. */
static inline char hx711_gain_channel(int gain)
{
    if (gain == 32)
        return 'B';
    if (gain == 128 || gain == 64)
        return 'A';
    return '?';
}

/* Counts to weight: (raw - offset) / scale; 0 for an uncalibrated scale. */
static inline float hx711_to_units(int32_t raw, int32_t offset, float scale)
{
    return loadcell_to_units(raw, offset, scale);
}

/* The scale from one known weight; refused under 100 counts of movement. */
#define HX711_MIN_CAL_DELTA  LOADCELL_MIN_CAL_DELTA

static inline bool hx711_solve_scale(int32_t raw, int32_t offset,
                                     float known_weight, float *out)
{
    return loadcell_solve_scale(raw, offset, known_weight, out);
}

/* Pinned at either end of the range: saturated at 800000h or 7FFFFFh
 * ("Serial Interface", p.4), with one count of margin. */
static inline bool hx711_saturated(int32_t raw)
{
    return loadcell_saturated24(raw);
}
