/*
 * NAU7802: a scale, over I2C.
 *
 * Run once with CALIBRATE set, note the two numbers it prints, paste them in
 * below and clear the flag.
 */
#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nau7802.h"
#include "loadcell.h"
#include "loadcell_log.h"

#define PIN_SDA 21
#define PIN_SCL 22

#define CALIBRATE     1
#define KNOWN_WEIGHT  1.000f

#define OFFSET  0
#define SCALE   1.0f

static const char *TAG = "example";

void app_main(void)
{
    i2c_master_bus_config_t buscfg = {
        .i2c_port = -1,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&buscfg, &bus));

    nau7802_config_t cfg = {
        .gain = NAU7802_GAIN_128,     /* a load cell puts out millivolts */
        .rate = NAU7802_RATE_80SPS,
        .ldo  = NAU7802_LDO_3V3,
        .use_internal_ldo = true,     /* excite the bridge from the chip */
    };

    nau7802_handle_t cell;
    ESP_ERROR_CHECK(nau7802_init(bus, &cfg, &cell));

#if CALIBRATE
    ESP_LOGI(TAG, "keep the cell clear");
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_ERROR_CHECK(nau7802_tare(cell, 20, 1000));

    ESP_LOGI(TAG, "now put %.3f units on it", (double)KNOWN_WEIGHT);
    vTaskDelay(pdMS_TO_TICKS(8000));
    if (nau7802_calibrate(cell, KNOWN_WEIGHT, 20, 1000) != ESP_OK) {
        ESP_LOGE(TAG, "calibration failed -- was the mass actually on?");
        return;
    }
    ESP_LOGI(TAG, "put these in the source: OFFSET %ld, SCALE %.4f",
             (long)nau7802_offset(cell), (double)nau7802_scale(cell));
#else
    nau7802_set_offset(cell, OFFSET);
    nau7802_set_scale(cell, SCALE);
#endif

    /* The same cell as any load cell: one timestamped sample. */
    struct loadcell_sample sample;
    if (loadcell_read(nau7802_loadcell(cell), &sample, 1000) == 0)
        loadcell_log_sample(nau7802_loadcell(cell), &sample);

    for (;;) {
        float w;

        if (nau7802_read_units(cell, 10, &w, 1000) != ESP_OK) {
            ESP_LOGW(TAG, "read timed out");
            continue;
        }
        if (nau7802_last_saturated(cell)) {
            ESP_LOGW(TAG, "saturated -- overloaded, or the bridge is miswired");
            continue;
        }
        printf("%8.3f\n", w);
    }
}
