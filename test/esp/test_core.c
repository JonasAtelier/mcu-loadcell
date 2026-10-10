/*
 * Host-side check of the core on ESP-IDF: the cases are ../core_cases.h;
 * this file supplies the clock, esp_timer_get_time(), and the log prefix.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "loadcell.h"
#include "loadcell_log.h"

/* The ESP-IDF stub log puts the level letter first. */
#define LOG_PREFIX(level)	level " "

static void set_clock_us(int64_t us)
{
	stub_time_us = us;
}

static void advance_clock(void)
{
	stub_time_us += 1000;
}

#include "../core_cases.h"

int main(void)
{
	run_core_cases();
	printf("all core checks passed\n");
	return 0;
}
