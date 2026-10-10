/*
 * The calibration both original drivers carried, line for line the same,
 * now written once. Each chip's own tare/calibrate/read_units calls are
 * wrappers over these.
 */
#include <errno.h>
#include <stddef.h>

#include "loadcell.h"
#include "loadcell_log.h"
#include "loadcell_math.h"

static bool loadcell_bound(const struct loadcell_dev *loadcell)
{
	return loadcell && loadcell->ops && loadcell->ops->read_raw &&
	       loadcell->ops->is_ready;
}

int loadcell_is_ready(struct loadcell_dev *loadcell, bool *ready)
{
	if (!loadcell_bound(loadcell) || !ready)
		return -EINVAL;

	return loadcell->ops->is_ready(loadcell, ready);
}

int loadcell_read(struct loadcell_dev *loadcell,
		  struct loadcell_sample *sample, uint32_t timeout_ms)
{
	int32_t counts;
	int ret;

	if (!loadcell_bound(loadcell) || !sample)
		return -EINVAL;

	ret = loadcell->ops->read_raw(loadcell, &counts, timeout_ms);
	if (ret)
		return ret;
	sample->t_us = loadcell_time_us();
	sample->counts = counts;
	sample->units = loadcell_to_units(counts, loadcell->offset,
					  loadcell->scale);
	sample->saturated = loadcell->saturated;
	return 0;
}

int loadcell_read_average(struct loadcell_dev *loadcell, int n,
			  int32_t *counts, uint32_t timeout_ms)
{
	int64_t sum = 0;

	if (!loadcell_bound(loadcell) || !counts || n <= 0)
		return -EINVAL;

	for (int i = 0; i < n; i++) {
		int32_t v;
		int ret;

		ret = loadcell->ops->read_raw(loadcell, &v, timeout_ms);
		if (ret)
			return ret;
		sum += v;
	}
	*counts = (int32_t)(sum / n);
	return 0;
}

int loadcell_tare(struct loadcell_dev *loadcell, int n, uint32_t timeout_ms)
{
	int32_t zero;
	int ret;

	if (!loadcell_bound(loadcell) || n <= 0)
		return -EINVAL;

	ret = loadcell_read_average(loadcell, n, &zero, timeout_ms);
	if (ret)
		return ret;
	if (loadcell->saturated) {
		loadcell_err(loadcell,
			     "taring against a saturated reading; check the wiring");
		return -ERANGE;
	}
	loadcell->offset = zero;
	loadcell_info(loadcell, "tare offset %ld counts", (long)zero);
	return 0;
}

int loadcell_calibrate(struct loadcell_dev *loadcell, float known_weight,
		       int n, uint32_t timeout_ms)
{
	int32_t loaded;
	float scale;
	int ret;

	if (!loadcell_bound(loadcell) || n <= 0)
		return -EINVAL;

	ret = loadcell_read_average(loadcell, n, &loaded, timeout_ms);
	if (ret)
		return ret;
	if (!loadcell_solve_scale(loaded, loadcell->offset, known_weight,
				  &scale)) {
		loadcell_err(loadcell,
			     "cannot calibrate: reading moved %ld counts for "
			     "%.3f units. Tare first, and put the mass on "
			     "before calling this",
			     (long)(loaded - loadcell->offset),
			     (double)known_weight);
		return -EINVAL;
	}
	loadcell->scale = scale;
	loadcell_info(loadcell, "scale %.3f counts per unit", (double)scale);
	return 0;
}

int loadcell_read_units(struct loadcell_dev *loadcell, int n, float *units,
			uint32_t timeout_ms)
{
	int32_t raw;
	int ret;

	if (!loadcell_bound(loadcell) || !units)
		return -EINVAL;

	ret = loadcell_read_average(loadcell, n, &raw, timeout_ms);
	if (ret)
		return ret;
	*units = loadcell_to_units(raw, loadcell->offset, loadcell->scale);
	return 0;
}

void loadcell_set_offset(struct loadcell_dev *loadcell, int32_t offset)
{
	if (loadcell)
		loadcell->offset = offset;
}

void loadcell_set_scale(struct loadcell_dev *loadcell, float counts_per_unit)
{
	if (loadcell && counts_per_unit != 0.0f)
		loadcell->scale = counts_per_unit;
}

int32_t loadcell_offset(const struct loadcell_dev *loadcell)
{
	return loadcell ? loadcell->offset : 0;
}

float loadcell_scale(const struct loadcell_dev *loadcell)
{
	return loadcell ? loadcell->scale : 0.0f;
}

bool loadcell_last_saturated(const struct loadcell_dev *loadcell)
{
	return loadcell ? loadcell->saturated : false;
}
