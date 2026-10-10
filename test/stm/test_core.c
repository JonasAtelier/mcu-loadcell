/*
 * Host-side check of the core on the STM32 HAL: the cases are
 * ../core_cases.h; this file supplies the clock -- the weak default,
 * HAL_GetTick() milliseconds as microseconds -- and the log prefix.
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
#include "loadcell_log.h"

/* The HAL's lines are bare printf(), no level letter. */
#define LOG_PREFIX(level)	""

static void set_clock_us(int64_t us)
{
	stub_tick = (uint32_t)(us / 1000);
}

static void advance_clock(void)
{
	stub_tick += 1;
}

#include "../core_cases.h"

/* 32-bit milliseconds do not wrap in the 64-bit microseconds. */
static void test_clock_range(void)
{
	stub_tick = UINT32_MAX;
	assert(loadcell_time_us() == (int64_t)UINT32_MAX * 1000);
}

/* With CONFIG_LOADCELL_DEBUG on, as test/Makefile builds it, dbg prints. */
static void test_dbg(void)
{
	struct loadcell_dev cell = { .name = "fake" };

	loadcell_dbg(&cell, "d %d", 1);
	assert(!strcmp(stub_log_last, "loadcell: fake: d 1"));
}

int main(void)
{
	run_core_cases();
	test_clock_range();
	test_dbg();
	printf("all core checks passed\n");
	return 0;
}
