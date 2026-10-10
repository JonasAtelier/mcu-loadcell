/*
 * The conversions both chips share, split out so they can be compiled and
 * asserted on the host. Both are 24-bit two's complement converters; the
 * calibration arithmetic is the same pair either way. Each chip's _math.h
 * keeps its own names for these, as aliases.
 *
 * A load cell amplifier gives you counts. Counts become weight only through
 * two numbers you have to measure yourself: an offset (what it reads with
 * nothing on it) and a scale (how many counts one unit of weight moves it).
 * Neither can be derived from the chip, and both drift with temperature.
 */
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Full scale of a 24-bit converter: 800000h and 7FFFFFh. */
#define LOADCELL_COUNT_MIN  (-8388608)
#define LOADCELL_COUNT_MAX  ( 8388607)

/* The result is 24-bit two's complement in a 32-bit word. */
static inline int32_t loadcell_sext24(uint32_t v)
{
    v &= 0x00FFFFFFu;
    return (int32_t)(v ^ 0x00800000u) - 0x00800000;
}

/*
 * Counts to weight: (raw - offset) / scale.
 *
 * scale is counts per unit -- kilograms, grams, newtons, whatever you
 * calibrated in. A scale of 0 means uncalibrated, and returns 0 rather than
 * dividing.
 */
static inline float loadcell_to_units(int32_t raw, int32_t offset, float scale)
{
    if (!(scale > 0.0f) && !(scale < 0.0f))
        return 0.0f;
    return ((float)raw - (float)offset) / scale;
}

/*
 * Solve for the scale from one known weight: put a mass on, read the counts,
 * and this gives the counts-per-unit to store.
 *
 * Refuses a zero or negative known weight, and refuses a reading that barely
 * moved from the offset -- both produce an absurd scale that then makes every
 * later reading nonsense.
 *
 * The movement floor is 100 counts. A load cell's full scale is millions of
 * counts, so a calibration mass that shifts it by less than a hundred is
 * being measured almost entirely in noise, and the scale derived from it
 * would carry that noise into every reading forever. In practice a delta this
 * small means the mass was never put on.
 */
#define LOADCELL_MIN_CAL_DELTA  100.0f

static inline bool loadcell_solve_scale(int32_t raw, int32_t offset,
                                        float known_weight, float *out)
{
    float delta = (float)raw - (float)offset;

    if (!(known_weight > 0.0f))
        return false;
    if (fabsf(delta) < LOADCELL_MIN_CAL_DELTA)
        return false;

    *out = delta / known_weight;
    return true;
}

/*
 * A reading pinned at either end of the 24-bit range is saturated: the cell is
 * overloaded, wired wrong, or the excitation is missing. The value looks like
 * a number and is not one. Both originals allowed one count of margin.
 */
static inline bool loadcell_saturated24(int32_t raw)
{
    return raw <= LOADCELL_COUNT_MIN + 1 || raw >= LOADCELL_COUNT_MAX - 1;
}
