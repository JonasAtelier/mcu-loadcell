/*
 * A fake HX711 on its two wires, and this library's cases against it,
 * shared by esp/test_hx711_shared.c and stm/test_hx711.c. The fake watches
 * the clock as the chip does: a falling edge after a short high phase is a
 * pulse, shifting out one bit, MSB first, then the trailing pulses that pick
 * the next gain (Table 3); a high phase of 60 us or more is a power-down,
 * and the falling edge that ends it a reset (Fig. 3). It also checks every
 * pulse's high phase ran with interrupts masked. Time is the platform's
 * own: each platform file provides
 *
 *   RET(err)               what the handle API returns for a negative errno
 *   base_cfg()             a config on that platform's pins, gain 128
 *   now_us()               simulated time, moved by the delays and polls
 *   masked()               whether interrupts are masked right now
 *   wire_sck(level)        call from its SCK write
 *   wire_dout()            return from its DOUT read
 *
 * esp-hx711-loadcell's own test, esp/test_hx711.c, covers the driver on
 * ESP-IDF against its own fake; these cases cover what this library adds --
 * the loadcell_dev view, the masking on both platforms, time-based
 * deadlines -- and the same driver on the HAL.
 */

#include "hx711_math.h"
#include "hx711_proto.h"

/* ------------------------------------------------------- the fake chip --- */

static int64_t now_us(void);
static bool masked(void);

static int sck;
static int dout_level = 1;
static uint32_t frame;			/* the 24-bit word being shifted out */
static int bits_left, trailing;
static bool just_pulsed, absent;
static int32_t next_value;
static int64_t rise_us, ready_at_us, conv_us;
static bool rise_masked;
static int pulses, power_downs, unmasked_pulses;

/* The next conversion: ready conv_us from now. */
static void load_frame(void)
{
	frame = (uint32_t)next_value & 0x00FFFFFFu;
	bits_left = HX711_DATA_BITS;
	trailing = 0;
	ready_at_us = now_us() + conv_us;
}

static void chip_reset(int32_t value)
{
	sck = 0;
	dout_level = 1;
	just_pulsed = absent = false;
	next_value = value;
	conv_us = 0;
	pulses = power_downs = unmasked_pulses = 0;
	load_frame();
}

static void wire_sck(int level)
{
	if (level && !sck) {
		rise_us = now_us();
		rise_masked = masked();
	} else if (!level && sck) {
		if (now_us() - rise_us >= 60) {
			/* Down since the rise; this edge wakes it, reset. */
			power_downs++;
			sck = 0;
			load_frame();
			return;
		}
		if (!rise_masked || !masked())
			unmasked_pulses++;
		if (bits_left > 0) {
			dout_level = (frame >> (bits_left - 1)) & 1;
			bits_left--;
		} else {
			trailing++;
			dout_level = 1;		/* high again after the frame */
		}
		pulses++;
		just_pulsed = true;
	}
	sck = level;
}

static int wire_dout(void)
{
	if (absent)
		return 1;
	if (just_pulsed) {
		/* the driver sampling the bit it just clocked out */
		just_pulsed = false;
		return dout_level;
	}
	if (bits_left == 0 && trailing > 0)
		load_frame();	/* clock idle, frame done: the next one */
	return bits_left == HX711_DATA_BITS && now_us() >= ready_at_us ? 0 : 1;
}

/* ------------------------------------------------------ allocations ------ */

void *__real_calloc(size_t n, size_t size);
void __real_free(void *p);
void *__wrap_calloc(size_t n, size_t size);
void __wrap_free(void *p);

static int allocs, frees;

void *__wrap_calloc(size_t n, size_t size)
{
	allocs++;
	return __real_calloc(n, size);
}

void __wrap_free(void *p)
{
	if (p)
		frees++;
	__real_free(p);
}

/* ------------------------------------------------------------ helpers ---- */

static hx711_handle_t open_ok(int32_t value)
{
	hx711_config_t c = base_cfg();
	hx711_handle_t h = NULL;

	chip_reset(value);
	assert(hx711_init(&c, &h) == RET(0));
	assert(h);
	return h;
}

/* ------------------------------------------------------------- cases ---- */

static void test_init(void)
{
	struct loadcell_dev *cell;
	hx711_handle_t h;

	allocs = frees = 0;
	h = open_ok(0);
	/* Power cycled once, so the part starts from a known state. */
	assert(power_downs == 1);
	cell = hx711_loadcell(h);
	assert(cell && !strcmp(cell->name, "hx711") && cell->ops &&
	       cell->priv == h && cell->scale == 1.0f && cell->offset == 0);
	assert(hx711_loadcell(NULL) == NULL);
	/* del powers it down again. */
	hx711_del(h);
	assert(power_downs == 1 && sck == 1);
	assert(allocs == 1 && frees == 1);
}

static void test_read_through_the_category(void)
{
	hx711_handle_t h = open_ok(-12345);
	struct loadcell_dev *cell = hx711_loadcell(h);
	struct loadcell_sample s;
	bool ready = false;
	float u;

	assert(loadcell_is_ready(cell, &ready) == 0 && ready);
	assert(loadcell_read(cell, &s, 100) == 0);
	assert(s.counts == -12345 && !s.saturated);

	/* One offset and scale, whichever API sets or reads them. */
	next_value = 1000;
	assert(loadcell_tare(cell, 2, 100) == 0);
	assert(hx711_offset(h) == 1000);
	next_value = 3000;
	assert(hx711_calibrate(h, 2.0f, 2, 100) == RET(0));
	assert(fabsf(loadcell_scale(cell) - 1000.0f) < 1e-3f);
	next_value = 2000;
	assert(loadcell_read_units(cell, 1, &u, 100) == 0);
	assert(fabsf(u - 1.0f) < 1e-4f);

	/* Saturated: reported, and the tare refused through both APIs. */
	next_value = HX711_COUNT_MAX;
	assert(loadcell_read(cell, &s, 100) == 0 && s.saturated);
	assert(loadcell_tare(cell, 1, 100) == -ERANGE);
	assert(hx711_tare(h, 1, 100) == RET(-ERANGE));
	assert(hx711_offset(h) == 1000);
	hx711_del(h);
}

/*
 * Every pulse's high phase ran masked and short, and nothing was left
 * masked: 24 data pulses and one trailing, and never a power-down.
 */
static void test_pulses_are_masked(void)
{
	hx711_handle_t h = open_ok(0x5A5A5);
	int32_t v;

	pulses = power_downs = unmasked_pulses = 0;
	assert(hx711_read_raw(h, &v, 100) == RET(0));
	assert(v == 0x5A5A5);
	assert(pulses == 25 && trailing == 1);
	assert(unmasked_pulses == 0 && power_downs == 0 && !masked());

	assert(hx711_read_average(h, 8, &v, 100) == RET(0) && v == 0x5A5A5);
	assert(unmasked_pulses == 0 && power_downs == 0 && !masked());
	hx711_del(h);
}

/* 25, 26 and 27 pulses for A128, B32 and A64 (Table 3). */
static void test_gain_pulses(void)
{
	hx711_config_t c = base_cfg();
	hx711_handle_t h = NULL;
	int32_t v;

	chip_reset(7);
	c.gain = HX711_GAIN_B32;
	assert(hx711_init(&c, &h) == RET(0));
	assert(hx711_read_raw(h, &v, 100) == RET(0) && trailing == 2);
	assert(hx711_set_gain(h, HX711_GAIN_A64, 100) == RET(0));
	assert(hx711_read_raw(h, &v, 100) == RET(0) && trailing == 3);
	assert(hx711_set_gain(h, HX711_GAIN_A128, 100) == RET(0));
	assert(hx711_read_raw(h, &v, 100) == RET(0) && trailing == 1);
	assert(hx711_set_gain(h, (hx711_gain_t)7, 100) == RET(-EINVAL));
	hx711_del(h);
}

/*
 * The deadline is time, not polls. The originals waited
 * vTaskDelay(pdMS_TO_TICKS(2)) -- nothing at all at 100 Hz -- and counted
 * 2 ms per poll, so a 1000 ms timeout ran out after 500 polls with no time
 * gone, and a conversion 900 ms away was never waited for.
 */
static void test_deadline_is_time(void)
{
	hx711_handle_t h = open_ok(4242);
	int64_t start;
	int32_t v = 0;

	conv_us = 900000;
	ready_at_us = now_us() + conv_us;
	start = now_us();
	assert(hx711_read_raw(h, &v, 1000) == RET(0) && v == 4242);
	assert(now_us() - start >= 900000);

	/* 1100 ms away: a timeout, but only once 1000 ms have passed. */
	conv_us = 1100000;
	ready_at_us = now_us() + conv_us;
	start = now_us();
	assert(hx711_read_raw(h, &v, 1000) == RET(-ETIMEDOUT));
	assert(now_us() - start >= 1000000);
	conv_us = 0;
	hx711_del(h);
}

static void test_absent(void)
{
	hx711_config_t c = base_cfg();
	hx711_handle_t h = NULL;
	int64_t start;

	/* DOUT never goes low: a timeout, nothing leaked. */
	chip_reset(0);
	absent = true;
	c.gain = HX711_GAIN_B32;	/* forces a throwaway read during init */
	allocs = frees = 0;
	start = now_us();
	assert(hx711_init(&c, &h) == RET(-ETIMEDOUT));
	assert(!h && allocs == 1 && frees == 1);
	assert(now_us() - start >= 1000000);
	assert(strstr(stub_log_last, "no HX711 responding"));
}

static void test_bad_config(void)
{
	hx711_config_t c = base_cfg();
	hx711_handle_t h = NULL;

	chip_reset(0);
	allocs = 0;
	c.gain = (hx711_gain_t)100;
	assert(hx711_init(&c, &h) == RET(-EINVAL));
	assert(strstr(stub_log_last, "hx711: gain must be 128, 64 or 32, not 100"));
	assert(hx711_init(NULL, &h) == RET(-EINVAL));
	c = base_cfg();
	assert(hx711_init(&c, NULL) == RET(-EINVAL));
	assert(allocs == 0 && !h);
}

static void test_power(void)
{
	hx711_handle_t h = open_ok(1);
	int32_t v;

	power_downs = 0;
	assert(hx711_power_down(h) == RET(0));
	assert(hx711_power_up(h) == RET(0));
	assert(power_downs == 1);
	assert(hx711_read_raw(h, &v, 100) == RET(0) && v == 1);
	assert(hx711_power_down(NULL) == RET(-EINVAL));
	assert(hx711_del(NULL) == RET(-EINVAL));
	hx711_del(h);
}

static void run_hx711_cases(void)
{
	test_init();
	test_read_through_the_category();
	test_pulses_are_masked();
	test_gain_pulses();
	test_deadline_is_time();
	test_absent();
	test_bad_config();
	test_power();
}
