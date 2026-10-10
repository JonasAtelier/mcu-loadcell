/*
 * HX711 protocol constants.
 *
 * There are no registers. The part has two wires -- a data line it drives and
 * a clock line you drive -- and everything is decided by how many times you
 * pulse the clock.
 *
 * Every value is quoted from the AVIA HX711 datasheet in docs/hx711.pdf, with
 * its page, table or figure, except the one marked as this driver's own
 * choice.
 */
#pragma once

/* 24 bits, two's complement, most significant first ("Serial Interface",
 * p.4; output data coding 800000h-7FFFFFh, Table 2, p.3). */
#define HX711_DATA_BITS        24

/*
 * Trailing clock pulses after the 24 data bits select what the NEXT
 * conversion measures: 25, 26 or 27 pulses in all (Table 3, p.4).
 *
 *   1 pulse  -> channel A, gain 128
 *   2 pulses -> channel B, gain 32
 *   3 pulses -> channel A, gain 64
 *
 * So the gain you ask for takes effect one reading later, not immediately.
 * Fig. 2 (p.5) labels 27 pulses "CH.B, Gain:64"; Table 3 says channel A,
 * and channel B has a fixed gain of 32 (p.4). Table 3 is followed.
 */
#define HX711_PULSES_A_128     1
#define HX711_PULSES_B_32      2
#define HX711_PULSES_A_64      3

/* After a reset or power-down the part is on channel A at gain 128 ("Reset
 * and Power-Down", p.5). */
#define HX711_RESET_GAIN       128

/*
 * Holding the clock line high for more than 60 us powers the chip down
 * (Fig. 3, p.5). That is how power-down is requested -- and it is also what
 * a badly timed preemption does by accident. See docs/chips.md.
 */
#define HX711_POWER_DOWN_US    64

/* The driver's own choice: the datasheet gives no wake time, only that the
 * part resets when PD_SCK returns low (p.5). The wait for DOUT low that
 * follows is what gates. */
#define HX711_POWER_UP_US      10

/* PD_SCK high (T3) 0.2 us minimum, 1 typical, 50 maximum; low (T4) 0.2 us
 * minimum, 1 typical (timing table under Fig. 2, p.5). 1 us each is the
 * typical figure, and far under T3's 50 us ceiling. */
#define HX711_CLOCK_HALF_US    1

/* Conversions per second, set by the RATE pin on the module: 10 with RATE
 * low, 80 high (Table 1, p.2; Table 2, p.3). */
#define HX711_RATE_10HZ        10
#define HX711_RATE_80HZ        80
