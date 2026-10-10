#ifndef FREERTOS_H
#define FREERTOS_H

#include <assert.h>
#include <stdint.h>

/*
 * Host stub for FreeRTOS: ticks, for the bus port's UART timeouts and the
 * chips' polling. The tick rate is a variable so a test can move it; it
 * starts at ESP-IDF's default 100 Hz (see idf_stubs.c), where
 * pdMS_TO_TICKS() of anything under 10 ms is 0. Same macros as the real one;
 * pdMS_TO_TICKS truncates as IDF's does.
 */
extern unsigned int stub_tick_hz;

#define configTICK_RATE_HZ	stub_tick_hz
#define portTICK_PERIOD_MS	((TickType_t)1000 / configTICK_RATE_HZ)
#define portMAX_DELAY		0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) \
	((TickType_t)((TickType_t)(ms) * configTICK_RATE_HZ / 1000U))
#define pdTRUE			1

typedef uint32_t TickType_t;
typedef int BaseType_t;

/*
 * The spinlock the HX711's clock pulses take. It does not lock anything --
 * the host tests are single-threaded -- but it asserts that every enter has
 * its exit and that nobody enters twice, and stub_critical says whether a
 * section is open right now.
 */
#define STUB_MUX_UNLOCKED	0x5A
#define STUB_MUX_LOCKED		0xA5

typedef struct {
	int state;
} portMUX_TYPE;

extern int stub_critical;	/* 1 inside a critical section */
extern int stub_critical_entries;

#define portMUX_INITIALIZER_UNLOCKED	{ STUB_MUX_UNLOCKED }
#define portENTER_CRITICAL(m) \
	do { assert((m)->state == STUB_MUX_UNLOCKED); \
	     (m)->state = STUB_MUX_LOCKED; stub_critical = 1; \
	     stub_critical_entries++; } while (0)
#define portEXIT_CRITICAL(m) \
	do { assert((m)->state == STUB_MUX_LOCKED); \
	     (m)->state = STUB_MUX_UNLOCKED; stub_critical = 0; } while (0)

#endif
