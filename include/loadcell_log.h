/*
 * Kernel dev_err()-style logging for loadcell drivers and their users.
 *
 * ESP-IDF: everything goes out under the one ESP-IDF tag "loadcell", prefixed
 * with the device's name, so one esp_log_level_set("loadcell", ...) controls
 * every chip and a line still says which chip it came from. There is
 * deliberately no level machinery of our own; loadcell_dbg() is also subject
 * to CONFIG_LOG_MAXIMUM_LEVEL, and CONFIG_LOADCELL_DEBUG is not used.
 *
 * STM32 HAL: every line goes out through printf() as "loadcell: <name>: ...",
 * so a line still says which chip it came from. Where printf() ends up is
 * the app's business: with newlib, retarget _write() to a UART. There is
 * deliberately no level machinery; loadcell_dbg() alone is compiled out
 * unless CONFIG_LOADCELL_DEBUG is 1 (stm-forge: LOADCELL_DEBUG = y).
 */
#ifndef LOADCELL_LOG_H
#define LOADCELL_LOG_H

#ifdef ESP_PLATFORM
#include "esp_log.h"
#else
#include <stdio.h>
#endif

#include "loadcell.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ESP_PLATFORM
#define LOADCELL_LOG_TAG	"loadcell"

#define loadcell_err(loadcell, fmt, ...) \
	ESP_LOGE(LOADCELL_LOG_TAG, "%s: " fmt, (loadcell)->name, ##__VA_ARGS__)
#define loadcell_warn(loadcell, fmt, ...) \
	ESP_LOGW(LOADCELL_LOG_TAG, "%s: " fmt, (loadcell)->name, ##__VA_ARGS__)
#define loadcell_info(loadcell, fmt, ...) \
	ESP_LOGI(LOADCELL_LOG_TAG, "%s: " fmt, (loadcell)->name, ##__VA_ARGS__)
#define loadcell_dbg(loadcell, fmt, ...) \
	ESP_LOGD(LOADCELL_LOG_TAG, "%s: " fmt, (loadcell)->name, ##__VA_ARGS__)

#else /* STM32 HAL */
#define loadcell_err(loadcell, fmt, ...) \
	printf("loadcell: %s: " fmt "\n", (loadcell)->name, ##__VA_ARGS__)
#define loadcell_warn(loadcell, fmt, ...) \
	printf("loadcell: %s: " fmt "\n", (loadcell)->name, ##__VA_ARGS__)
#define loadcell_info(loadcell, fmt, ...) \
	printf("loadcell: %s: " fmt "\n", (loadcell)->name, ##__VA_ARGS__)

#if CONFIG_LOADCELL_DEBUG
#define loadcell_dbg(loadcell, fmt, ...) \
	printf("loadcell: %s: " fmt "\n", (loadcell)->name, ##__VA_ARGS__)
#else
/* Still type-checked, so a debug line cannot rot while it is off. */
#define loadcell_dbg(loadcell, fmt, ...) do { \
	if (0) \
		printf("loadcell: %s: " fmt "\n", (loadcell)->name, \
		       ##__VA_ARGS__); \
} while (0)
#endif
#endif /* ESP_PLATFORM */

/* One info line: timestamp, counts, units, and "saturated" when it was. */
void loadcell_log_sample(const struct loadcell_dev *loadcell,
			 const struct loadcell_sample *sample);

#ifdef __cplusplus
}
#endif

#endif /* LOADCELL_LOG_H */
