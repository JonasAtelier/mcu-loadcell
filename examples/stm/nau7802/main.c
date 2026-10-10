/*
 * NAU7802: a scale over I2C, tare and known-mass calibration included.
 *
 * A snippet, not a project: the clock tree, the printf() retarget and the
 * I2C bus are the app's -- here hi2c1 as CubeMX generates it
 * (MX_I2C1_Init()), at up to 400 kHz, with pull-ups on SDA and SCL. The
 * chip is fixed at 0x2A: one per bus. Needs HAL_I2C_MODULE_ENABLED and
 * LOADCELL_NAU7802 = y; printing the floats needs -u _printf_float.
 */
#include CMSIS_device_header

#include "loadcell.h"
#include "loadcell_log.h"
#include "nau7802.h"

#define KNOWN_WEIGHT	1.000f		/* whatever mass you have */

extern I2C_HandleTypeDef hi2c1;		/* from the app's MX_I2C1_Init() */

void scale_task(void)
{
	const nau7802_config_t cfg = {
		.gain = NAU7802_GAIN_128,	/* a load cell puts out millivolts */
		.rate = NAU7802_RATE_80SPS,
		.ldo = NAU7802_LDO_3V3,
		.use_internal_ldo = true,	/* excite the bridge from the chip */
	};
	struct loadcell_dev *cell;
	nau7802_handle_t h;

	/* -ENXIO: nothing at 0x2A. -EPROTO: the chip's calibration failed. */
	if (nau7802_init(&hi2c1, &cfg, &h))
		return;
	cell = nau7802_loadcell(h);

	HAL_Delay(2000);			/* keep the cell clear */
	if (loadcell_tare(cell, 20, 1000))
		return;
	HAL_Delay(8000);			/* now put KNOWN_WEIGHT on */
	if (loadcell_calibrate(cell, KNOWN_WEIGHT, 20, 1000))
		return;				/* the mass was not on */

	for (;;) {
		struct loadcell_sample s;

		if (!loadcell_read(cell, &s, 1000))
			loadcell_log_sample(cell, &s);
	}
}
