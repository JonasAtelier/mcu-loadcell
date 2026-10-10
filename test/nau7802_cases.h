/*
 * A fake NAU7802 register file, and this library's cases against it, shared
 * by esp/test_nau7802_shared.c and stm/test_nau7802.c. The fake keeps the
 * datasheet's start-up order: RR resets every register (11.1), PUR only
 * once PUD is set (9.1), CALS clears itself after a few polls and reports
 * through CAL_ERR (11.3), and CR only once CS is set and the conversion's
 * time has come. Each platform file puts it on its own I2C calls and
 * provides:
 *
 *   RET(err)               what the handle API returns for a negative errno
 *   BUS                    the bus argument of nau7802_init()
 *   now_us()               simulated time, moved by the polls
 *   bus_fail               true: every transfer fails as a NACK would
 *
 * esp-nau7802-loadcell's own test, esp/test_nau7802.c, covers the driver
 * on ESP-IDF; these cases cover what this library adds -- the presence
 * probe, the loadcell_dev view, time-based deadlines, the error codes --
 * and the same driver on the HAL.
 */

#include "nau7802_math.h"
#include "nau7802_regs.h"

/* ------------------------------------------------------- the fake chip --- */

static int64_t now_us(void);

static uint8_t regs[0x20];
static bool present = true;
static bool bus_fail;
static bool pur_never, cal_never, cal_should_fail;
static int resets, cal_runs, cal_polls_left;
static int32_t sample;
static int64_t ready_at_us;

static void chip_reset(int32_t value)
{
	memset(regs, 0, sizeof(regs));
	regs[NAU7802_REVISION] = 0x0F;
	present = true;
	bus_fail = pur_never = cal_never = cal_should_fail = false;
	resets = cal_runs = cal_polls_left = 0;
	sample = value;
	ready_at_us = 0;
}

static void chip_write(uint8_t reg, uint8_t v)
{
	assert(reg < sizeof(regs));
	if (reg == NAU7802_PU_CTRL) {
		if (v & NAU7802_RR) {
			memset(regs, 0, sizeof(regs));
			regs[NAU7802_REVISION] = 0x0F;
			resets++;
			regs[NAU7802_PU_CTRL] = v;
			return;
		}
		/* PUR only asserts once the digital section is up. */
		if ((v & NAU7802_PUD) && !pur_never)
			v |= NAU7802_PUR;
		regs[NAU7802_PU_CTRL] = v;
		return;
	}
	if (reg == NAU7802_CTRL2 && (v & NAU7802_CALS)) {
		cal_runs++;
		cal_polls_left = 3;
	}
	regs[reg] = v;
}

static void chip_read(uint8_t reg, uint8_t *buf, size_t len)
{
	if (reg == NAU7802_CTRL2 && cal_polls_left > 0 && !cal_never &&
	    --cal_polls_left == 0) {
		regs[NAU7802_CTRL2] &= (uint8_t)~NAU7802_CALS;
		if (cal_should_fail)
			regs[NAU7802_CTRL2] |= NAU7802_CAL_ERR;
	}
	if (reg == NAU7802_PU_CTRL && (regs[NAU7802_PU_CTRL] & NAU7802_CS) &&
	    now_us() >= ready_at_us) {
		regs[NAU7802_PU_CTRL] |= NAU7802_CR;
		regs[NAU7802_ADCO_B2] = (uint8_t)((uint32_t)sample >> 16);
		regs[NAU7802_ADCO_B2 + 1] = (uint8_t)((uint32_t)sample >> 8);
		regs[NAU7802_ADCO_B2 + 2] = (uint8_t)sample;
	}
	for (size_t i = 0; i < len; i++)
		buf[i] = regs[(reg + i) % sizeof(regs)];
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

static nau7802_config_t base_cfg(void)
{
	nau7802_config_t c = {
		.gain = NAU7802_GAIN_128,
		.rate = NAU7802_RATE_80SPS,
		.ldo = NAU7802_LDO_3V3,
		.use_internal_ldo = true,
	};

	return c;
}

static nau7802_handle_t open_ok(int32_t value)
{
	nau7802_config_t c = base_cfg();
	nau7802_handle_t h = NULL;

	chip_reset(value);
	assert(nau7802_init(BUS, &c, &h) == RET(0));
	assert(h);
	return h;
}

/* ------------------------------------------------------------- cases ---- */

/* Nothing at 0x2A: refused before anything is allocated or written. */
static void test_absent(void)
{
	nau7802_config_t c = base_cfg();
	nau7802_handle_t h = NULL;

	chip_reset(0);
	present = false;
	allocs = 0;
	assert(nau7802_init(BUS, &c, &h) == RET(-ENXIO));
	assert(!h && allocs == 0 && resets == 0);
	assert(strstr(stub_log_last, "nau7802: nothing answers at 0x2a"));
}

static void test_init(void)
{
	struct loadcell_dev *cell;
	nau7802_handle_t h;

	allocs = frees = 0;
	h = open_ok(0);
	assert(resets == 1 && cal_runs == 1);
	assert((regs[NAU7802_PU_CTRL] & (NAU7802_PUD | NAU7802_PUA |
					 NAU7802_CS | NAU7802_AVDDS)) ==
	       (NAU7802_PUD | NAU7802_PUA | NAU7802_CS | NAU7802_AVDDS));
	cell = nau7802_loadcell(h);
	assert(cell && !strcmp(cell->name, "nau7802") && cell->ops &&
	       cell->priv == h && cell->scale == 1.0f);
	assert(nau7802_loadcell(NULL) == NULL);
	nau7802_del(h);
	assert(regs[NAU7802_PU_CTRL] == 0);
	assert(allocs == 1 && frees == 1);
}

static void test_init_failures(void)
{
	nau7802_config_t c = base_cfg();
	nau7802_handle_t h = NULL;
	int64_t start;

	/* Answers, but never reports power-up ready: given 200 ms. */
	chip_reset(0);
	pur_never = true;
	allocs = frees = 0;
	start = now_us();
	assert(nau7802_init(BUS, &c, &h) == RET(-ENODEV));
	assert(now_us() - start >= 200000);
	assert(!h && allocs == 1 && frees == 1);

	/* The chip's calibration fails: -EPROTO, nothing leaked. */
	chip_reset(0);
	cal_should_fail = true;
	assert(nau7802_init(BUS, &c, &h) == RET(-EPROTO));
	assert(allocs == 2 && frees == 2);

	/* ...or never ends: given 400 ms. */
	chip_reset(0);
	cal_never = true;
	start = now_us();
	assert(nau7802_init(BUS, &c, &h) == RET(-ETIMEDOUT));
	assert(now_us() - start >= 400000);

	/* The bus failing. */
	chip_reset(0);
	bus_fail = true;
	assert(nau7802_init(BUS, &c, &h) == RET(-EIO));
	assert(allocs == frees);
}

/* The internal calibration asks for CALMOD 00 and leaves the rest of
 * CTRL2 -- rate, channel -- as it was. */
static void test_calmod(void)
{
	nau7802_handle_t h = open_ok(0);
	uint8_t crs = (uint8_t)(regs[NAU7802_CTRL2] & 0x70);

	regs[NAU7802_CTRL2] |= NAU7802_CALMOD_SYSTEM_GAIN;
	assert(NAU7802_CALMOD_MASK == 0x03);
	assert(nau7802_calibrate_internal(h) == RET(0));
	assert((regs[NAU7802_CTRL2] & NAU7802_CALMOD_MASK) ==
	       NAU7802_CALMOD_INTERNAL_OFFSET);
	assert((regs[NAU7802_CTRL2] & 0x70) == crs && crs != 0);
	nau7802_del(h);
}

static void test_read_through_the_category(void)
{
	nau7802_handle_t h = open_ok(-123456);
	struct loadcell_dev *cell = nau7802_loadcell(h);
	struct loadcell_sample s;
	bool ready = false;
	float u;

	assert(loadcell_is_ready(cell, &ready) == 0 && ready);
	assert(loadcell_read(cell, &s, 100) == 0);
	assert(s.counts == -123456 && !s.saturated);

	/* One offset and scale, whichever API sets or reads them. */
	sample = 1000;
	assert(nau7802_tare(h, 2, 100) == RET(0));
	assert(loadcell_offset(cell) == 1000);
	sample = 3000;
	assert(loadcell_calibrate(cell, 2.0f, 2, 100) == 0);
	assert(fabsf(nau7802_scale(h) - 1000.0f) < 1e-3f);
	sample = 2000;
	assert(nau7802_read_units(h, 1, &u, 100) == RET(0));
	assert(fabsf(u - 1.0f) < 1e-4f);

	sample = NAU7802_COUNT_MIN;
	assert(loadcell_read(cell, &s, 100) == 0 && s.saturated);
	assert(nau7802_tare(h, 1, 100) == RET(-ERANGE));
	nau7802_del(h);
}

/*
 * The deadline is time, not polls. The original waited
 * vTaskDelay(pdMS_TO_TICKS(2)) -- nothing at all at 100 Hz -- and counted
 * 2 ms per poll, so a conversion 900 ms away, at 10 SPS say, was never
 * waited for.
 */
static void test_deadline_is_time(void)
{
	nau7802_handle_t h = open_ok(4242);
	int64_t start;
	int32_t v = 0;

	ready_at_us = now_us() + 900000;
	regs[NAU7802_PU_CTRL] &= (uint8_t)~NAU7802_CR;
	start = now_us();
	assert(nau7802_read_raw(h, &v, 1000) == RET(0) && v == 4242);
	assert(now_us() - start >= 900000);

	ready_at_us = now_us() + 1100000;
	regs[NAU7802_PU_CTRL] &= (uint8_t)~NAU7802_CR;
	start = now_us();
	assert(nau7802_read_raw(h, &v, 1000) == RET(-ETIMEDOUT));
	assert(now_us() - start >= 1000000);
	nau7802_del(h);
}

static void test_registers(void)
{
	nau7802_handle_t h = open_ok(0);
	uint8_t b[3];

	assert(nau7802_reg_read(h, NAU7802_REVISION, b, 1) == RET(0));
	assert((b[0] & 0x0F) == 0x0F);
	assert(nau7802_reg_write(h, NAU7802_I2C_CTRL, 0x30) == RET(0));
	assert(regs[NAU7802_I2C_CTRL] == 0x30);

	/* Standby: PUA and PUD down, the rest kept; and back. */
	assert(nau7802_sleep(h, true) == RET(0));
	assert(!(regs[NAU7802_PU_CTRL] & (NAU7802_PUA | NAU7802_PUD)));
	assert(regs[NAU7802_PU_CTRL] & NAU7802_CS);
	assert(nau7802_sleep(h, false) == RET(0));
	assert(regs[NAU7802_PU_CTRL] & NAU7802_PUA);

	/* Channel and gain re-run the internal calibration. */
	cal_runs = 0;
	assert(nau7802_set_gain(h, NAU7802_GAIN_32) == RET(0) && cal_runs == 1);
	assert(nau7802_set_channel(h, 2, 100) == RET(0));
	assert((regs[NAU7802_CTRL2] & NAU7802_CHS) && cal_runs == 2);

	bus_fail = true;
	assert(nau7802_reg_read(h, NAU7802_REVISION, b, 1) == RET(-EIO));
	bus_fail = false;
	nau7802_del(h);
}

static void run_nau7802_cases(void)
{
	test_absent();
	test_init();
	test_init_failures();
	test_calmod();
	test_read_through_the_category();
	test_deadline_is_time();
	test_registers();
}
