/*
 * loadcell -- a small Linux-IIO-style load cell ADC subsystem for ESP-IDF
 * and the STM32 HAL.
 *
 * One struct loadcell_dev per converter. A load cell amplifier gives counts;
 * counts become weight only through two numbers you measure yourself, an
 * offset (what it reads with nothing on it) and a scale (counts per unit of
 * weight). Both chips here did that the same way, so it lives once, here:
 * tare, calibrate against a known mass, average, and convert to units.
 *
 * Each chip keeps its own handle API (hx711.h, nau7802.h). The handle holds
 * a struct loadcell_dev, and hx711_loadcell() / nau7802_loadcell() hand it
 * out; the chip's own tare, calibrate and offset calls work on the same
 * numbers. The handle's init fills it and its del ends it: there is no
 * probe or remove of its own.
 *
 * Every int-returning function returns 0 or a negative errno.
 */
#ifndef LOADCELL_H
#define LOADCELL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct loadcell_sample {
	int32_t counts;		/* the conversion, sign extended */
	float units;		/* (counts - offset) / scale */
	bool saturated;		/* counts at an end of the range: not a weight */
	int64_t t_us;		/* loadcell_time_us() when it was fetched */
};

struct loadcell_dev;

struct loadcell_ops {
	/* true once a conversion is waiting */
	int (*is_ready)(struct loadcell_dev *loadcell, bool *ready);
	/*
	 * Block for one conversion, at most timeout_ms, and set
	 * loadcell->saturated from it. -ETIMEDOUT if none comes.
	 */
	int (*read_raw)(struct loadcell_dev *loadcell, int32_t *counts,
			uint32_t timeout_ms);
};

struct loadcell_dev {
	const char *name;		/* "hx711"; log prefix */
	const struct loadcell_ops *ops;
	void *priv;			/* the chip's handle */
	int32_t offset;			/* counts with nothing on the cell */
	float scale;			/* counts per unit; 1 until calibrated */
	bool saturated;			/* the last conversion was */
};

/* *ready is true once a conversion is waiting. */
int loadcell_is_ready(struct loadcell_dev *loadcell, bool *ready);

/*
 * One conversion, at most timeout_ms away, as a sample. *sample is written
 * only on success. t_us is taken when the counts were fetched: the
 * conversion finished up to one poll earlier.
 */
int loadcell_read(struct loadcell_dev *loadcell,
		  struct loadcell_sample *sample, uint32_t timeout_ms);

/*
 * The mean of n conversions, each at most timeout_ms away. A single reading
 * is noisy; averaging is where the resolution comes from. -EINVAL for n <= 0.
 */
int loadcell_read_average(struct loadcell_dev *loadcell, int n,
			  int32_t *counts, uint32_t timeout_ms);

/*
 * With nothing on the cell: the mean of n conversions becomes the offset.
 * -ERANGE, offset unchanged, if the last of them was saturated: an
 * overloaded or miswired cell has no zero.
 */
int loadcell_tare(struct loadcell_dev *loadcell, int n, uint32_t timeout_ms);

/*
 * With known_weight on the cell, after a tare: the scale that turns the mean
 * of n conversions into known_weight. -EINVAL, scale unchanged, for a weight
 * that is not positive or a reading under 100 counts from the offset -- in
 * practice, the mass was never put on.
 */
int loadcell_calibrate(struct loadcell_dev *loadcell, float known_weight,
		       int n, uint32_t timeout_ms);

/* The mean of n conversions in the unit you calibrated with. */
int loadcell_read_units(struct loadcell_dev *loadcell, int n, float *units,
			uint32_t timeout_ms);

/*
 * Set the stored calibration at startup, rather than re-taring every boot.
 * A scale of 0 is ignored: it would divide by zero later.
 */
void loadcell_set_offset(struct loadcell_dev *loadcell, int32_t offset);
void loadcell_set_scale(struct loadcell_dev *loadcell, float counts_per_unit);
int32_t loadcell_offset(const struct loadcell_dev *loadcell);
float loadcell_scale(const struct loadcell_dev *loadcell);

/* Whether the last conversion sat at an end of the range. */
bool loadcell_last_saturated(const struct loadcell_dev *loadcell);

/*
 * The clock samples are stamped with, in microseconds. On ESP-IDF it is
 * esp_timer_get_time(). On the STM32 HAL the default is weak and only
 * HAL_GetTick() * 1000: millisecond steps that wrap after 49.7 days. Define
 * your own loadcell_time_us() for real microseconds.
 */
int64_t loadcell_time_us(void);

#ifdef __cplusplus
}
#endif

#endif /* LOADCELL_H */
