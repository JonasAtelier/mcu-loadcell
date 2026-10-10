/*
 * Host-side check of the HX711 driver on ESP-IDF against this library's
 * cases: the fake chip and the cases are ../hx711_cases.h; this file puts
 * the chip on the GPIO calls, keeps a clock that moves only in
 * esp_rom_delay_us() and vTaskDelay(), at ESP-IDF's default 100 Hz tick,
 * and adds what only ESP-IDF has. esp-hx711-loadcell's own test is
 * esp/test_hx711.c.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hx711.h"
#include "loadcell.h"
#include "loadcell_priv.h"

#define DOUT_PIN	4
#define SCK_PIN		5

#define RET(err)	loadcell_esp_err(err)

static int64_t sim_us;
static gpio_mode_t dout_mode, sck_mode;

static hx711_config_t base_cfg(void)
{
	hx711_config_t c = {
		.dout_gpio = DOUT_PIN,
		.sck_gpio = SCK_PIN,
		.gain = HX711_GAIN_A128,
	};

	return c;
}

static int64_t now_us(void)
{
	return sim_us;
}

static bool masked(void)
{
	return stub_critical != 0;
}

#include "../hx711_cases.h"

void esp_rom_delay_us(uint32_t us)
{
	sim_us += us;
}

void vTaskDelay(TickType_t ticks)
{
	sim_us += (int64_t)ticks * 1000000 / stub_tick_hz;
}

TickType_t xTaskGetTickCount(void)
{
	return (TickType_t)(sim_us * stub_tick_hz / 1000000);
}

esp_err_t gpio_config(const gpio_config_t *c)
{
	if (c->pin_bit_mask == 1ULL << DOUT_PIN)
		dout_mode = c->mode;
	else if (c->pin_bit_mask == 1ULL << SCK_PIN)
		sck_mode = c->mode;
	else
		assert(0 && "a pin that is not ours");
	return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t g, uint32_t level)
{
	assert(g == SCK_PIN);
	wire_sck((int)level);
	return ESP_OK;
}

int gpio_get_level(gpio_num_t g)
{
	assert(g == DOUT_PIN);
	return wire_dout();
}

/* The driver sets the pins up itself, as esp-hx711-loadcell did; the
 * original's checks of the GPIO numbers, and the codes, still hold. */
static void test_esp_pins(void)
{
	hx711_config_t c = base_cfg();
	hx711_handle_t h;

	h = open_ok(0);
	assert(dout_mode == GPIO_MODE_INPUT && sck_mode == GPIO_MODE_OUTPUT);
	hx711_del(h);

	c.dout_gpio = -1;
	assert(hx711_init(&c, &h) == ESP_ERR_INVALID_ARG);
	c = base_cfg();
	c.sck_gpio = c.dout_gpio;
	assert(hx711_init(&c, &h) == ESP_ERR_INVALID_ARG);
	assert(RET(-ERANGE) == ESP_ERR_INVALID_STATE);
	assert(RET(-ETIMEDOUT) == ESP_ERR_TIMEOUT);
}

int main(void)
{
	assert(stub_tick_hz == 100);
	run_hx711_cases();
	test_esp_pins();
	printf("all hx711 checks passed\n");
	return 0;
}
