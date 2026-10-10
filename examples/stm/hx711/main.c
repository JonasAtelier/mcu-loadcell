/*
 * HX711: a scale on two GPIO pins, tare and known-mass calibration included.
 *
 * A snippet, not a project: the clock tree, the printf() retarget and the
 * pins are the app's -- here DOUT and PD_SCK as CubeMX generates them
 * (MX_GPIO_Init()): DOUT an input, PD_SCK a push-pull output, low. The
 * driver times the clock pulses on SysTick, which HAL_Init() starts, and
 * masks interrupts for each pulse's microsecond high phase. Needs
 * LOADCELL_HX711 = y; printing the floats needs -u _printf_float.
 */
#include CMSIS_device_header

#include "hx711.h"
#include "loadcell.h"
#include "loadcell_log.h"

#define DOUT_PORT	GPIOA		/* the app's choices */
#define DOUT_PIN	GPIO_PIN_0
#define SCK_PORT	GPIOA
#define SCK_PIN		GPIO_PIN_1

#define KNOWN_WEIGHT	1.000f		/* whatever mass you have */

void scale_task(void)
{
	const hx711_config_t cfg = {
		.dout_port = DOUT_PORT,
		.dout_pin = DOUT_PIN,
		.sck_port = SCK_PORT,
		.sck_pin = SCK_PIN,
		.gain = HX711_GAIN_A128,	/* channel A, most sensitive */
	};
	struct loadcell_dev *cell;
	hx711_handle_t h;

	/* -ETIMEDOUT: DOUT never went low -- power, or DOUT and SCK swapped. */
	if (hx711_init(&cfg, &h))
		return;
	cell = hx711_loadcell(h);

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
