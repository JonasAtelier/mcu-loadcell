/*
 * HX711: a scale, including the calibration you cannot skip.
 *
 * Run it once with CALIBRATE set. It tares, asks you to put a known mass on,
 * and prints the two numbers. Put those in OFFSET and SCALE, clear CALIBRATE,
 * and it weighs things from boot.
 */
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hx711.h"
#include "loadcell.h"
#include "loadcell_log.h"

#define PIN_DOUT  16
#define PIN_SCK   17

/* Set to 1 for the first run, then paste the printed numbers in below. */
#define CALIBRATE      1
#define KNOWN_WEIGHT   1.000f      /* whatever mass you have, in your units */

#define OFFSET   0
#define SCALE    1.0f

static const char *TAG = "example";

void app_main(void)
{
    hx711_config_t cfg = {
        .dout_gpio = PIN_DOUT,
        .sck_gpio  = PIN_SCK,
        .gain      = HX711_GAIN_A128,   /* channel A, most sensitive */
    };

    hx711_handle_t cell;
    ESP_ERROR_CHECK(hx711_init(&cfg, &cell));

#if CALIBRATE
    ESP_LOGI(TAG, "taking the cell unloaded -- keep it clear");
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_ERROR_CHECK(hx711_tare(cell, 20, 1000));

    ESP_LOGI(TAG, "now put %.3f units on it", (double)KNOWN_WEIGHT);
    vTaskDelay(pdMS_TO_TICKS(8000));
    if (hx711_calibrate(cell, KNOWN_WEIGHT, 20, 1000) != ESP_OK) {
        ESP_LOGE(TAG, "calibration failed -- was the mass actually on?");
        return;
    }
    ESP_LOGI(TAG, "put these in the source: OFFSET %ld, SCALE %.4f",
             (long)hx711_offset(cell), (double)hx711_scale(cell));
#else
    hx711_set_offset(cell, OFFSET);
    hx711_set_scale(cell, SCALE);
#endif

    /* The same cell as any load cell: one timestamped sample. */
    struct loadcell_sample sample;
    if (loadcell_read(hx711_loadcell(cell), &sample, 1000) == 0)
        loadcell_log_sample(hx711_loadcell(cell), &sample);

    for (;;) {
        float w;

        /* Averaging is where the resolution comes from; a single conversion
         * is noisy. Ten at 10 Hz is one reading per second. */
        if (hx711_read_units(cell, 10, &w, 1000) != ESP_OK) {
            ESP_LOGW(TAG, "read timed out");
            continue;
        }
        if (hx711_last_saturated(cell)) {
            ESP_LOGW(TAG, "saturated -- overloaded, or the bridge is miswired");
            continue;
        }
        printf("%8.3f\n", w);
    }
}
