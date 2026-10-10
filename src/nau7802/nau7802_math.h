/*
 * Conversions and calibration, host testable. All values are from Nuvoton's
 * datasheet revision 1.7, section 11. The calibration arithmetic is the one
 * both chips share, in loadcell_math.h; these are its NAU7802 names, which
 * esp-nau7802-loadcell's own tests use.
 */
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "loadcell_math.h"
#include "nau7802_regs.h"

/* The converter is 24 bit, two's complement. */
#define NAU7802_COUNT_MIN  LOADCELL_COUNT_MIN
#define NAU7802_COUNT_MAX  LOADCELL_COUNT_MAX

static inline int32_t nau7802_sext24(uint32_t v)
{
    return loadcell_sext24(v);
}

/* Result registers are big endian, most significant byte first (0x12..0x14). */
static inline int32_t nau7802_raw(const uint8_t *p)
{
    return nau7802_sext24(((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]);
}

/* GAINS[2:0] -> PGA gain. Section 11.2: 000 is x1 and each step doubles. */
static inline int nau7802_gain_value(int code)
{
    return (code >= 0 && code <= 7) ? (1 << code) : 0;
}

/*
 * CRS[2:0] -> samples per second, section 11.3.
 *
 * Note the table is NOT contiguous: codes 0 to 3 are 10, 20, 40 and 80 SPS,
 * then 320 SPS jumps to code 7 with 4, 5 and 6 undefined. Treating the code
 * as an index into a dense array puts you at the wrong rate.
 */
static inline int nau7802_rate_sps(int code)
{
    switch (code) {
    case 0: return 10;
    case 1: return 20;
    case 2: return 40;
    case 3: return 80;
    case 7: return 320;
    default: return 0;
    }
}

/*
 * VLDO[2:0] -> internal LDO output in millivolts, section 11.2. This one runs
 * backwards: 000 is the HIGHEST voltage, 4.5 V, and 111 the lowest at 2.4 V.
 */
static inline int nau7802_ldo_mv(int code)
{
    static const int mv[8] = { 4500, 4200, 3900, 3600, 3300, 3000, 2700, 2400 };

    return (code >= 0 && code <= 7) ? mv[code] : 0;
}

/* Conversion period in milliseconds, with a little margin. */
static inline int nau7802_conv_time_ms(int rate_code)
{
    int sps = nau7802_rate_sps(rate_code);

    return sps > 0 ? (1000 / sps) + 1 : 0;
}

/*
 * Counts to weight: (raw - offset) / scale, exactly as for any bridge.
 * A scale of zero means uncalibrated and returns 0 rather than dividing.
 */
static inline float nau7802_to_units(int32_t raw, int32_t offset, float scale)
{
    return loadcell_to_units(raw, offset, scale);
}

/*
 * Solve the scale from one known mass. Refused if the reading barely moved:
 * a 24-bit bridge reads in the millions of counts, so a delta under a hundred
 * means the mass was never on the cell, and the scale from it would be noise
 * carried into every later reading.
 */
#define NAU7802_MIN_CAL_DELTA LOADCELL_MIN_CAL_DELTA

static inline bool nau7802_solve_scale(int32_t raw, int32_t offset,
                                       float known_weight, float *out)
{
    return loadcell_solve_scale(raw, offset, known_weight, out);
}

/* A reading pinned at either end is a saturated amplifier, not a weight. */
static inline bool nau7802_saturated(int32_t raw)
{
    return loadcell_saturated24(raw);
}
