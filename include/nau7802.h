/*
 * nau7802 -- the Nuvoton NAU7802 24-bit bridge ADC, as a load cell chip
 * driver, for ESP-IDF and the STM32 HAL.
 *
 * The same job as an HX711 -- read a load cell -- over I2C instead of a
 * bit-banged two-wire protocol. That difference is the whole point: no
 * microsecond timing, no critical sections, and no way for a task switch to
 * corrupt a reading.
 *
 * It also brings an internal LDO to excite the bridge, a PGA to x128, two
 * channels, and hardware offset calibration.
 *
 * The I2C bus is the app's: on ESP-IDF an i2c_master bus it has created, to
 * which the driver adds its device; on the STM32 HAL an I2C handle it has
 * HAL_I2C_Init()ed. nau7802_loadcell() hands out the struct loadcell_dev
 * inside the handle; tare, calibrate, offset and scale are the same numbers
 * through either API.
 *
 * On ESP-IDF every call returns an esp_err_t, as esp-nau7802-loadcell did;
 * on the STM32 HAL, 0 or a negative errno.
 *
 * Every register value comes from Nuvoton's datasheet revision 1.7, included
 * as docs/nau7802.pdf. Nothing here has been run against a physical chip.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "driver/i2c_master.h"
#include "esp_err.h"
#else
#include <stddef.h>

#include CMSIS_device_header
#endif

#include "loadcell.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nau7802_t *nau7802_handle_t;

typedef enum {
    NAU7802_GAIN_1   = 0,
    NAU7802_GAIN_2   = 1,
    NAU7802_GAIN_4   = 2,
    NAU7802_GAIN_8   = 3,
    NAU7802_GAIN_16  = 4,
    NAU7802_GAIN_32  = 5,
    NAU7802_GAIN_64  = 6,
    NAU7802_GAIN_128 = 7,   /* what a load cell usually wants */
} nau7802_gain_t;

/* Note the gap: there is no code 4, 5 or 6. */
typedef enum {
    NAU7802_RATE_10SPS  = 0,
    NAU7802_RATE_20SPS  = 1,
    NAU7802_RATE_40SPS  = 2,
    NAU7802_RATE_80SPS  = 3,
    NAU7802_RATE_320SPS = 7,
} nau7802_rate_t;

/* Internal LDO, used to excite the bridge. Codes run backwards: 0 is 4.5 V. */
typedef enum {
    NAU7802_LDO_4V5 = 0,
    NAU7802_LDO_4V2 = 1,
    NAU7802_LDO_3V9 = 2,
    NAU7802_LDO_3V6 = 3,
    NAU7802_LDO_3V3 = 4,
    NAU7802_LDO_3V0 = 5,
    NAU7802_LDO_2V7 = 6,
    NAU7802_LDO_2V4 = 7,
} nau7802_ldo_t;

typedef struct {
    uint32_t       scl_hz;      /* 0 selects 400 kHz. ESP-IDF only: on the
                                 * HAL the clock is the handle's */
    nau7802_gain_t gain;
    nau7802_rate_t rate;
    nau7802_ldo_t  ldo;         /* ignored unless use_internal_ldo */
    bool           use_internal_ldo;  /* drive the bridge from the chip */
    int            channel;     /* 1 or 2; 0 selects 1 */
} nau7802_config_t;

#ifdef ESP_PLATFORM
/*
 * Reset, power up, wait for the analog section to report ready, configure,
 * run the internal offset calibration and start converting.
 *
 * ESP_ERR_NOT_FOUND if nothing answers at 0x2A or the part never reports
 * power-up ready.
 * ESP_ERR_INVALID_STATE if the internal calibration reports an error.
 * ESP_ERR_INVALID_ARG for a gain, rate, LDO or channel outside the enums.
 */
esp_err_t nau7802_init(i2c_master_bus_handle_t bus, const nau7802_config_t *cfg,
                       nau7802_handle_t *out);
esp_err_t nau7802_del(nau7802_handle_t dev);

/* True once a conversion is waiting. */
esp_err_t nau7802_is_ready(nau7802_handle_t dev, bool *ready);

/* Block for one conversion, sign extended from 24 bits. */
esp_err_t nau7802_read_raw(nau7802_handle_t dev, int32_t *value, uint32_t timeout_ms);

/* Average of n conversions. A single reading is noisy; averaging is where
 * the resolution comes from. */
esp_err_t nau7802_read_average(nau7802_handle_t dev, int n, int32_t *value,
                               uint32_t timeout_ms);

/* ---- calibration ----
 *
 * Two different things share the word, and both matter:
 *
 *   nau7802_calibrate_internal()  the CHIP's own offset trim, in hardware
 *   nau7802_tare() / _calibrate() YOUR zero and scale, to turn counts into
 *                                 weight -- the same pair an HX711 needs
 */
esp_err_t nau7802_calibrate_internal(nau7802_handle_t dev);

esp_err_t nau7802_tare(nau7802_handle_t dev, int n, uint32_t timeout_ms);
esp_err_t nau7802_calibrate(nau7802_handle_t dev, float known_weight, int n,
                            uint32_t timeout_ms);
esp_err_t nau7802_read_units(nau7802_handle_t dev, int n, float *units,
                             uint32_t timeout_ms);

void    nau7802_set_offset(nau7802_handle_t dev, int32_t offset);
void    nau7802_set_scale(nau7802_handle_t dev, float counts_per_unit);
int32_t nau7802_offset(nau7802_handle_t dev);
float   nau7802_scale(nau7802_handle_t dev);

/* True if the last reading sat at an end of the 24-bit range: an overloaded
 * cell, a miswired bridge, or missing excitation. Not a weight. */
bool nau7802_last_saturated(nau7802_handle_t dev);

/* Switch channel. The part has two, and switching needs a fresh conversion
 * before the reading belongs to the new one. */
esp_err_t nau7802_set_channel(nau7802_handle_t dev, int channel,
                              uint32_t timeout_ms);

esp_err_t nau7802_set_gain(nau7802_handle_t dev, nau7802_gain_t gain);
esp_err_t nau7802_set_rate(nau7802_handle_t dev, nau7802_rate_t rate);

/* Power down both the analog and digital sections. */
esp_err_t nau7802_sleep(nau7802_handle_t dev, bool sleep);

esp_err_t nau7802_reg_read(nau7802_handle_t dev, uint8_t reg, void *buf, size_t len);
esp_err_t nau7802_reg_write(nau7802_handle_t dev, uint8_t reg, uint8_t value);
#else
/*
 * The same start-up as on ESP-IDF, on an I2C handle the app has
 * HAL_I2C_Init()ed. -EINVAL for a NULL handle or a gain, rate, LDO or
 * channel outside the enums; -ENXIO if nothing answers at 0x2A; -ENODEV if
 * the part never reports power-up ready; -EPROTO if the internal
 * calibration reports an error.
 */
int nau7802_init(I2C_HandleTypeDef *bus, const nau7802_config_t *cfg,
                 nau7802_handle_t *out);
int nau7802_del(nau7802_handle_t dev);

int nau7802_is_ready(nau7802_handle_t dev, bool *ready);
int nau7802_read_raw(nau7802_handle_t dev, int32_t *value, uint32_t timeout_ms);
int nau7802_read_average(nau7802_handle_t dev, int n, int32_t *value,
                         uint32_t timeout_ms);

int nau7802_calibrate_internal(nau7802_handle_t dev);

/* -ERANGE if the cell reads saturated; see loadcell_tare(). */
int nau7802_tare(nau7802_handle_t dev, int n, uint32_t timeout_ms);
int nau7802_calibrate(nau7802_handle_t dev, float known_weight, int n,
                      uint32_t timeout_ms);
int nau7802_read_units(nau7802_handle_t dev, int n, float *units,
                       uint32_t timeout_ms);

void    nau7802_set_offset(nau7802_handle_t dev, int32_t offset);
void    nau7802_set_scale(nau7802_handle_t dev, float counts_per_unit);
int32_t nau7802_offset(nau7802_handle_t dev);
float   nau7802_scale(nau7802_handle_t dev);
bool    nau7802_last_saturated(nau7802_handle_t dev);

int nau7802_set_channel(nau7802_handle_t dev, int channel,
                        uint32_t timeout_ms);
int nau7802_set_gain(nau7802_handle_t dev, nau7802_gain_t gain);
int nau7802_set_rate(nau7802_handle_t dev, nau7802_rate_t rate);
int nau7802_sleep(nau7802_handle_t dev, bool sleep);

int nau7802_reg_read(nau7802_handle_t dev, uint8_t reg, void *buf, size_t len);
int nau7802_reg_write(nau7802_handle_t dev, uint8_t reg, uint8_t value);
#endif

/* The handle as a struct loadcell_dev; NULL for a NULL handle. Valid until
 * nau7802_del(). */
struct loadcell_dev *nau7802_loadcell(nau7802_handle_t dev);

#ifdef __cplusplus
}
#endif
