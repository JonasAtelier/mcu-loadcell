/*
 * Host-side check of the HX711 driver on the STM32 HAL: the fake chip and
 * the cases are ../hx711_cases.h; this file puts the chip on
 * HAL_GPIO_WritePin()/ReadPin(), keeps time in the stub's SysTick cycles
 * and HAL_Delay() milliseconds, and adds what only the HAL has: the pins as
 * ports, and PRIMASK.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32_hal_stub.h"

#include "hx711.h"
#include "loadcell.h"
#include "loadcell_priv.h"

#define RET(err)	(err)

static GPIO_TypeDef port_a, port_b;

static hx711_config_t base_cfg(void)
{
	hx711_config_t c = {
		.dout_port = &port_a,
		.dout_pin = GPIO_PIN_4,
		.sck_port = &port_b,
		.sck_pin = GPIO_PIN_4,
		.gain = HX711_GAIN_A128,
	};

	return c;
}

/* SysTick cycles at 16 MHz, plus HAL_Delay()'s milliseconds. */
static int64_t now_us(void)
{
	return (int64_t)(stub_cycles / 16) + (int64_t)stub_tick * 1000;
}

static bool masked(void)
{
	return stub_primask != 0;
}

#include "../hx711_cases.h"

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
	assert(port == &port_b && pin == GPIO_PIN_4);
	wire_sck(state == GPIO_PIN_SET);
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
	assert(port == &port_a && pin == GPIO_PIN_4);
	return wire_dout() ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

/* Ports, not GPIO numbers: NULL, or both pins the same, is refused. */
static void test_stm_pins(void)
{
	hx711_config_t c = base_cfg();
	hx711_handle_t h = NULL;

	allocs = 0;
	c.dout_port = NULL;
	assert(hx711_init(&c, &h) == -EINVAL);
	c = base_cfg();
	c.sck_port = NULL;
	assert(hx711_init(&c, &h) == -EINVAL);
	c = base_cfg();
	c.sck_port = c.dout_port;
	assert(hx711_init(&c, &h) == -EINVAL);
	assert(allocs == 0 && !h);
	/* Same pin number on another port is two pins. */
	c = base_cfg();
	chip_reset(0);
	assert(hx711_init(&c, &h) == 0);
	hx711_del(h);
	/* PRIMASK put back as found. */
	assert(stub_primask == 0 && stub_irq_masks > 0);
}

int main(void)
{
	assert(SystemCoreClock == 16000000);
	run_hx711_cases();
	test_stm_pins();
	printf("all hx711 checks passed\n");
	return 0;
}
