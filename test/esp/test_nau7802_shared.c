/*
 * Host-side check of the NAU7802 driver on ESP-IDF against this library's
 * cases: the fake chip and the cases are ../nau7802_cases.h; this file puts
 * the chip on the i2c_master calls, keeps a clock that moves only in
 * vTaskDelay(), at ESP-IDF's default 100 Hz tick, and adds what only
 * ESP-IDF has. esp-nau7802-loadcell's own test is esp/test_nau7802.c.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "loadcell.h"
#include "loadcell_priv.h"
#include "nau7802.h"

#define RET(err)	loadcell_esp_err(err)
#define BUS		((i2c_master_bus_handle_t)(void *)&bus_token)

static int bus_token;
static int64_t sim_us;
static i2c_device_config_t devcfg;
static int added;

static int64_t now_us(void)
{
	return sim_us;
}

#include "../nau7802_cases.h"

void vTaskDelay(TickType_t ticks)
{
	sim_us += (int64_t)ticks * 1000000 / stub_tick_hz;
}

TickType_t xTaskGetTickCount(void)
{
	return (TickType_t)(sim_us * stub_tick_hz / 1000000);
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address,
			   int timeout_ms)
{
	(void)timeout_ms;
	assert(bus == BUS && address == NAU7802_ADDR);
	return present ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
				    const i2c_device_config_t *cfg,
				    i2c_master_dev_handle_t *out)
{
	assert(bus == BUS);
	devcfg = *cfg;
	added++;
	*out = (i2c_master_dev_handle_t)(void *)&devcfg;
	return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev)
{
	assert(dev == (i2c_master_dev_handle_t)(void *)&devcfg);
	added--;
	return ESP_OK;
}

/* A NACK in the middle of a transfer is ESP_ERR_INVALID_STATE in IDF. */
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data,
			      size_t len, int t)
{
	(void)dev;
	(void)t;
	assert(len == 2);
	if (bus_fail)
		return ESP_ERR_INVALID_STATE;
	chip_write(data[0], data[1]);
	return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev,
				      const uint8_t *tx, size_t tx_len,
				      uint8_t *rx, size_t rx_len, int t)
{
	(void)dev;
	(void)t;
	assert(tx_len == 1);
	if (bus_fail)
		return ESP_ERR_INVALID_STATE;
	chip_read(tx[0], rx, rx_len);
	return ESP_OK;
}

/* The device added at 0x2A, at 400 kHz or scl_hz, removed again -- also
 * after a failed init. */
static void test_esp_device(void)
{
	nau7802_config_t c = base_cfg();
	nau7802_handle_t h;

	h = open_ok(0);
	assert(added == 1 && devcfg.device_address == 0x2A &&
	       devcfg.scl_speed_hz == 400000);
	nau7802_del(h);
	assert(added == 0);
	chip_reset(0);
	c.scl_hz = 100000;
	assert(nau7802_init(BUS, &c, &h) == ESP_OK);
	assert(devcfg.scl_speed_hz == 100000);
	nau7802_del(h);
	chip_reset(0);
	cal_should_fail = true;
	assert(nau7802_init(BUS, &c, &h) == ESP_ERR_INVALID_STATE);
	assert(added == 0);
	/* What the header promises: nothing at 0x2A is NOT_FOUND. */
	chip_reset(0);
	present = false;
	assert(nau7802_init(BUS, &c, &h) == ESP_ERR_NOT_FOUND);
}

int main(void)
{
	assert(stub_tick_hz == 100);
	run_nau7802_cases();
	test_esp_device();
	printf("all nau7802 checks passed\n");
	return 0;
}
