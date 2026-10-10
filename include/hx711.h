/*
 * hx711 -- the Avia HX711 24-bit load cell amplifier, as a load cell chip
 * driver, for ESP-IDF and the STM32 HAL.
 *
 * The chip behind every cheap scale. Two wires, no bus: the part drives a
 * data line and you drive a clock, and the number of clock pulses decides
 * both what you read and what it measures next.
 *
 * Because it is bit-banged, this driver disables interrupts around each clock
 * pulse. That is not defensive -- holding the clock high for 60 us powers the
 * chip down, and a task switch at the wrong moment does exactly that. See
 * docs/chips.md.
 *
 * The two pins: on ESP-IDF the driver sets them up itself with gpio_config();
 * on the STM32 HAL the app has made DOUT an input and SCK a push-pull output,
 * and the driver only reads and drives them. hx711_loadcell() hands out the
 * struct loadcell_dev inside the handle; tare, calibrate, offset and scale
 * are the same numbers through either API.
 *
 * On ESP-IDF every call returns an esp_err_t, as esp-hx711-loadcell did; on
 * the STM32 HAL, 0 or a negative errno.
 *
 * Nothing here has been run against a physical chip.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "esp_err.h"
#else
#include CMSIS_device_header
#endif

#include "loadcell.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx711_t *hx711_handle_t;

/*
 * Gain also picks the input. A and B are separate load cell inputs, and B is
 * only available at gain 32 -- so on a two-cell board you cannot have both at
 * full sensitivity.
 */
typedef enum {
    HX711_GAIN_A128 = 128,   /* channel A, the usual choice */
    HX711_GAIN_A64  = 64,    /* channel A, half the sensitivity */
    HX711_GAIN_B32  = 32,    /* channel B */
} hx711_gain_t;

typedef struct {
#ifdef ESP_PLATFORM
    gpio_num_t   dout_gpio;   /* the chip drives this */
    gpio_num_t   sck_gpio;    /* you drive this */
#else
    GPIO_TypeDef *dout_port;  /* an input the app has set up */
    uint16_t     dout_pin;
    GPIO_TypeDef *sck_port;   /* a push-pull output the app has set up */
    uint16_t     sck_pin;
#endif
    hx711_gain_t gain;
} hx711_config_t;

#ifdef ESP_PLATFORM
/*
 * Set the pins up, power cycle the part and take one throwaway reading so the
 * requested gain is in force.
 *
 * ESP_ERR_INVALID_ARG for a bad pin or gain.
 * ESP_ERR_TIMEOUT if the data line never goes low -- nothing wired, no power
 * to the module, or DOUT and SCK swapped.
 */
esp_err_t hx711_init(const hx711_config_t *cfg, hx711_handle_t *out);
esp_err_t hx711_del(hx711_handle_t dev);

/* True when a conversion is waiting. The part pulls DOUT low to say so. */
esp_err_t hx711_is_ready(hx711_handle_t dev, bool *ready);

/* Block until a conversion is ready and return it, sign extended from 24
 * bits. ESP_ERR_TIMEOUT if nothing arrives within timeout_ms. */
esp_err_t hx711_read_raw(hx711_handle_t dev, int32_t *value, uint32_t timeout_ms);

/* Average of n conversions. A single HX711 reading is noisy; averaging is
 * how the advertised resolution is actually reached. */
esp_err_t hx711_read_average(hx711_handle_t dev, int n, int32_t *value,
                             uint32_t timeout_ms);

/* ---- calibration ----
 *
 * Two numbers turn counts into weight, and you have to measure both:
 *
 *   1. hx711_tare()      with nothing on the cell   -> the offset
 *   2. hx711_calibrate() with a known mass on it    -> the scale
 *
 * Store them and set them at startup with hx711_set_offset/_set_scale, or the
 * user re-tares every boot.
 */
esp_err_t hx711_tare(hx711_handle_t dev, int n, uint32_t timeout_ms);
esp_err_t hx711_calibrate(hx711_handle_t dev, float known_weight, int n,
                          uint32_t timeout_ms);

void  hx711_set_offset(hx711_handle_t dev, int32_t offset);
void  hx711_set_scale(hx711_handle_t dev, float counts_per_unit);
int32_t hx711_offset(hx711_handle_t dev);
float hx711_scale(hx711_handle_t dev);

/* Weight in whatever unit you calibrated with. */
esp_err_t hx711_read_units(hx711_handle_t dev, int n, float *units,
                           uint32_t timeout_ms);

/*
 * True if the last reading sat at either end of the 24-bit range. That is a
 * saturated amplifier -- overloaded cell, miswired bridge, or missing
 * excitation -- and the number it returned is not a measurement.
 */
bool hx711_last_saturated(hx711_handle_t dev);

/* Power down draws under 1 uA. Waking resets the gain to 128, so the driver
 * restores the configured gain with a throwaway reading. */
esp_err_t hx711_power_down(hx711_handle_t dev);
esp_err_t hx711_power_up(hx711_handle_t dev);

/* Change gain and input. Takes effect on the conversion after next, so this
 * clocks a throwaway reading through to make it current. */
esp_err_t hx711_set_gain(hx711_handle_t dev, hx711_gain_t gain, uint32_t timeout_ms);
#else
/*
 * Power cycle the part and take one throwaway reading so the requested gain
 * is in force, as on ESP-IDF. -EINVAL for a NULL port, both pins the same,
 * or a bad gain; -ETIMEDOUT if the data line never goes low.
 */
int hx711_init(const hx711_config_t *cfg, hx711_handle_t *out);
int hx711_del(hx711_handle_t dev);

int hx711_is_ready(hx711_handle_t dev, bool *ready);
int hx711_read_raw(hx711_handle_t dev, int32_t *value, uint32_t timeout_ms);
int hx711_read_average(hx711_handle_t dev, int n, int32_t *value,
                       uint32_t timeout_ms);

/* -ERANGE if the cell reads saturated; see loadcell_tare(). */
int hx711_tare(hx711_handle_t dev, int n, uint32_t timeout_ms);
int hx711_calibrate(hx711_handle_t dev, float known_weight, int n,
                    uint32_t timeout_ms);

void  hx711_set_offset(hx711_handle_t dev, int32_t offset);
void  hx711_set_scale(hx711_handle_t dev, float counts_per_unit);
int32_t hx711_offset(hx711_handle_t dev);
float hx711_scale(hx711_handle_t dev);

int hx711_read_units(hx711_handle_t dev, int n, float *units,
                     uint32_t timeout_ms);
bool hx711_last_saturated(hx711_handle_t dev);

int hx711_power_down(hx711_handle_t dev);
int hx711_power_up(hx711_handle_t dev);
int hx711_set_gain(hx711_handle_t dev, hx711_gain_t gain, uint32_t timeout_ms);
#endif

/* The handle as a struct loadcell_dev; NULL for a NULL handle. Valid until
 * hx711_del(). */
struct loadcell_dev *hx711_loadcell(hx711_handle_t dev);

#ifdef __cplusplus
}
#endif
