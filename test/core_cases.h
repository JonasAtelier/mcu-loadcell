/*
 * The core's cases -- loadcell_read(), averaging, tare, calibration, units,
 * the stored offset and scale, and loadcell_log_sample() -- shared by
 * esp/test_core.c and stm/test_core.c. Each includes this after providing:
 *
 *   set_clock_us(us)       what loadcell_time_us() returns next
 *   advance_clock()        move that clock on, as a conversion would
 *   LOG_PREFIX             what the platform's log puts before "loadcell: "
 *   stub_log_last          the last log line
 *
 * What each chip's ops do lives in its driver, so each test_<chip>.c checks
 * that against the real driver.
 */

#define FAKE_MAX 16

static int32_t fake_counts[FAKE_MAX];	/* what the next reads return */
static int fake_next, fake_len;
static int fake_ret;			/* fail the next read with this */
static int fake_reads;
static uint32_t fake_timeout;

static int fake_is_ready(struct loadcell_dev *loadcell, bool *ready)
{
	(void)loadcell;
	*ready = fake_next < fake_len;
	return 0;
}

static int fake_read_raw(struct loadcell_dev *loadcell, int32_t *counts,
			 uint32_t timeout_ms)
{
	fake_reads++;
	fake_timeout = timeout_ms;
	/* The conversion takes time: a stamp taken before it would show. */
	advance_clock();
	if (fake_ret)
		return fake_ret;
	assert(fake_next < fake_len);
	*counts = fake_counts[fake_next++];
	loadcell->saturated = *counts <= -8388607 || *counts >= 8388606;
	return 0;
}

static const struct loadcell_ops fake_ops = {
	.is_ready = fake_is_ready,
	.read_raw = fake_read_raw,
};

/* Queue the counts the next reads return. */
static void fake_queue(int n, const int32_t *counts)
{
	assert(n <= FAKE_MAX);
	for (int i = 0; i < n; i++)
		fake_counts[i] = counts[i];
	fake_next = 0;
	fake_len = n;
	fake_ret = 0;
	fake_reads = 0;
}

static void fake_queue_same(int n, int32_t counts)
{
	assert(n <= FAKE_MAX);
	for (int i = 0; i < n; i++)
		fake_counts[i] = counts;
	fake_next = 0;
	fake_len = n;
	fake_ret = 0;
	fake_reads = 0;
}

static struct loadcell_dev fake_dev(void)
{
	struct loadcell_dev loadcell = {
		.name = "fake",
		.ops = &fake_ops,
		.scale = 1.0f,
	};

	fake_queue(0, NULL);
	return loadcell;
}

static void test_read(void)
{
	struct loadcell_dev cell = fake_dev();
	struct loadcell_sample s = { .t_us = -1 };
	bool ready;

	assert(loadcell_is_ready(&cell, &ready) == 0 && !ready);
	fake_queue_same(1, 12345);
	assert(loadcell_is_ready(&cell, &ready) == 0 && ready);

	/* Stamped when the counts came, after the conversion. */
	set_clock_us(5000000);
	cell.offset = 345;
	cell.scale = 1000.0f;
	assert(loadcell_read(&cell, &s, 77) == 0);
	assert(fake_timeout == 77);
	assert(s.counts == 12345 && s.units == 12.0f && !s.saturated);
	assert(s.t_us > 5000000);

	/* Saturated is reported in the sample and kept for later. */
	fake_queue_same(1, 8388607);
	assert(loadcell_read(&cell, &s, 10) == 0);
	assert(s.saturated && loadcell_last_saturated(&cell));

	/* A failed read leaves the sample alone. */
	s.counts = 7;
	fake_ret = -ETIMEDOUT;
	assert(loadcell_read(&cell, &s, 10) == -ETIMEDOUT);
	assert(s.counts == 7);
}

static void test_average(void)
{
	static const int32_t mixed[4] = { 100, 200, -300, 400 };
	struct loadcell_dev cell = fake_dev();
	int32_t v = 0;

	fake_queue(4, mixed);
	assert(loadcell_read_average(&cell, 4, &v, 10) == 0);
	assert(v == 100 && fake_reads == 4);

	/* Summed in 64 bits: four readings near full scale do not wrap. */
	fake_queue_same(4, 8000000);
	assert(loadcell_read_average(&cell, 4, &v, 10) == 0 && v == 8000000);

	/* A failure is returned, and *counts left alone. */
	fake_queue_same(2, 1);
	fake_ret = -EIO;
	v = 99;
	assert(loadcell_read_average(&cell, 2, &v, 10) == -EIO && v == 99);

	assert(loadcell_read_average(&cell, 0, &v, 10) == -EINVAL);
	assert(loadcell_read_average(&cell, -1, &v, 10) == -EINVAL);
	assert(loadcell_read_average(&cell, 1, NULL, 10) == -EINVAL);
}

static void test_calibration(void)
{
	struct loadcell_dev cell = fake_dev();
	float u = 0.0f;

	/* Tare with nothing on it. */
	fake_queue_same(4, 1000);
	assert(loadcell_tare(&cell, 4, 100) == 0);
	assert(loadcell_offset(&cell) == 1000);
	assert(strstr(stub_log_last, "loadcell: fake: tare offset 1000 counts"));

	/* Two units on: 3000 counts, so 1000 counts per unit. */
	fake_queue_same(4, 3000);
	assert(loadcell_calibrate(&cell, 2.0f, 4, 100) == 0);
	assert(fabsf(loadcell_scale(&cell) - 1000.0f) < 1e-3f);
	fake_queue_same(1, 2000);
	assert(loadcell_read_units(&cell, 1, &u, 100) == 0);
	assert(fabsf(u - 1.0f) < 1e-4f);

	/* The mass never put on: refused, the good scale kept. */
	fake_queue_same(4, 1050);
	assert(loadcell_calibrate(&cell, 2.0f, 4, 100) == -EINVAL);
	assert(strstr(stub_log_last, "cannot calibrate: reading moved 50 counts"));
	assert(fabsf(loadcell_scale(&cell) - 1000.0f) < 1e-3f);
	fake_queue_same(4, 3000);
	assert(loadcell_calibrate(&cell, 0.0f, 4, 100) == -EINVAL);
	assert(loadcell_calibrate(&cell, 2.0f, 0, 100) == -EINVAL);

	/* A saturated cell has no zero: refused, the offset kept. */
	fake_queue_same(2, -8388608);
	assert(loadcell_tare(&cell, 2, 100) == -ERANGE);
	assert(loadcell_offset(&cell) == 1000);
	assert(loadcell_tare(&cell, 0, 100) == -EINVAL);

	/* Stored values, set at boot; a zero scale would divide later. */
	loadcell_set_offset(&cell, 55);
	loadcell_set_scale(&cell, 7.5f);
	loadcell_set_scale(&cell, 0.0f);
	assert(loadcell_offset(&cell) == 55 && loadcell_scale(&cell) == 7.5f);
	assert(loadcell_read_units(&cell, 1, NULL, 100) == -EINVAL);
}

static void test_unbound(void)
{
	struct loadcell_dev cell = fake_dev();
	struct loadcell_sample s;
	int32_t v;
	float u;
	bool b;

	/* Nothing to call is the caller's mistake, and nothing is called. */
	assert(loadcell_read(NULL, &s, 10) == -EINVAL);
	assert(loadcell_read(&cell, NULL, 10) == -EINVAL);
	assert(loadcell_is_ready(&cell, NULL) == -EINVAL);
	cell.ops = NULL;
	assert(loadcell_read(&cell, &s, 10) == -EINVAL);
	assert(loadcell_is_ready(&cell, &b) == -EINVAL);
	assert(loadcell_read_average(&cell, 1, &v, 10) == -EINVAL);
	assert(loadcell_tare(&cell, 1, 10) == -EINVAL);
	assert(loadcell_calibrate(&cell, 1.0f, 1, 10) == -EINVAL);
	assert(loadcell_read_units(&cell, 1, &u, 10) == -EINVAL);
	assert(fake_reads == 0);

	assert(loadcell_offset(NULL) == 0 && loadcell_scale(NULL) == 0.0f);
	assert(!loadcell_last_saturated(NULL));
	loadcell_set_offset(NULL, 1);
	loadcell_set_scale(NULL, 1.0f);
}

static void test_log(void)
{
	struct loadcell_dev cell = fake_dev();
	struct loadcell_sample s = { .counts = -1234, .units = 1.5f,
				     .t_us = 42000 };

	loadcell_err(&cell, "no answer at 0x%02x (%d)", 0x2A, -6);
	assert(!strcmp(stub_log_last, LOG_PREFIX("E")
		       "loadcell: fake: no answer at 0x2a (-6)"));

	loadcell_log_sample(&cell, &s);
	assert(!strcmp(stub_log_last, LOG_PREFIX("I")
		       "loadcell: fake: t 42000 us     -1234 counts       "
		       "1.500 units"));
	s.saturated = true;
	s.t_us = INT64_MIN;
	loadcell_log_sample(&cell, &s);
	assert(strstr(stub_log_last, " t -9223372036854775808 us "));
	assert(strstr(stub_log_last, " units  saturated"));
}

static void run_core_cases(void)
{
	test_read();
	test_average();
	test_calibration();
	test_unbound();
	test_log();
}
