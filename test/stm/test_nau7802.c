/*
 * Host-side check of the NAU7802 driver on the STM32 HAL: the fake chip and
 * the cases are ../nau7802_cases.h; this file puts the chip on
 * HAL_I2C_IsDeviceReady()/Mem_Read()/Mem_Write(), with time in HAL_Delay()
 * milliseconds, and adds what only the HAL has.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32_hal_stub.h"

#include "loadcell.h"
#include "loadcell_priv.h"
#include "nau7802.h"

#define RET(err)	(err)
#define BUS		(&hi2c)

static I2C_HandleTypeDef hi2c;

static int64_t now_us(void)
{
	return (int64_t)stub_tick * 1000;
}

#include "../nau7802_cases.h"

/* The 7-bit 0x2A goes to the HAL shifted, as 0x54. */
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t addr,
					uint32_t trials, uint32_t t)
{
	(void)trials;
	(void)t;
	assert(h == &hi2c && addr == 0x54);
	return present ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t addr,
				    uint16_t reg, uint16_t reg_size,
				    uint8_t *data, uint16_t size, uint32_t t)
{
	(void)t;
	assert(h == &hi2c && addr == 0x54 && size == 1 &&
	       reg_size == I2C_MEMADD_SIZE_8BIT);
	if (bus_fail)
		return HAL_ERROR;
	chip_write((uint8_t)reg, data[0]);
	return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t addr,
				   uint16_t reg, uint16_t reg_size,
				   uint8_t *data, uint16_t size, uint32_t t)
{
	(void)t;
	assert(h == &hi2c && addr == 0x54 && reg_size == I2C_MEMADD_SIZE_8BIT);
	if (bus_fail)
		return HAL_ERROR;
	chip_read((uint8_t)reg, data, size);
	return HAL_OK;
}

static void test_null_handle(void)
{
	nau7802_config_t c = base_cfg();
	nau7802_handle_t h = NULL;

	allocs = 0;
	assert(nau7802_init(NULL, &c, &h) == -EINVAL);
	assert(!h && allocs == 0);
}

int main(void)
{
	run_nau7802_cases();
	test_null_handle();
	printf("all nau7802 checks passed\n");
	return 0;
}
