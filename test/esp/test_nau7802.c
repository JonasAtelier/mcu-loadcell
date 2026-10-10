/*
 * Host-side check against a fake NAU7802.
 *
 * The fake enforces the datasheet's start-up ordering: PUR does not assert
 * until PUD has been set, nothing converts until CS is set, and CALS clears
 * itself only after being polled. A driver that skips a step gets a device
 * that never becomes ready.
 */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nau7802.h"
#include "nau7802_math.h"
#include "nau7802_regs.h"

static uint8_t regs[64];
static uint16_t dev_addr;
static int  cal_polls_left, resets, cal_runs;
static bool cal_should_fail, never_ready;
static int32_t sample;

static void chip_reset(int32_t value)
{
    memset(regs, 0, sizeof(regs));
    regs[NAU7802_REVISION] = 0x0F;
    cal_polls_left = 0;
    resets = cal_runs = 0;
    cal_should_fail = false;
    never_ready = false;
    sample = value;
}

static void load_sample(void)
{
    regs[NAU7802_ADCO_B2]     = (uint8_t)(((uint32_t)sample >> 16) & 0xFF);
    regs[NAU7802_ADCO_B2 + 1] = (uint8_t)(((uint32_t)sample >> 8) & 0xFF);
    regs[NAU7802_ADCO_B2 + 2] = (uint8_t)((uint32_t)sample & 0xFF);
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t *cfg,
                                    i2c_master_dev_handle_t *out)
{
    (void)bus;
    dev_addr = cfg->device_address;
    *out = (i2c_master_dev_handle_t)(void *)regs;
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t d) { (void)d; return ESP_OK; }

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d, const uint8_t *data,
                              size_t len, int t)
{
    (void)d; (void)t;
    assert(len == 2);
    {
        uint8_t reg = data[0], v = data[1];

        if (reg == NAU7802_PU_CTRL) {
            if (v & NAU7802_RR) {
                memset(regs, 0, sizeof(regs));
                regs[NAU7802_REVISION] = 0x0F;
                resets++;
                regs[NAU7802_PU_CTRL] = v;
                return ESP_OK;
            }
            /* PUR only asserts once the digital section is up. */
            if (v & NAU7802_PUD)
                v = (uint8_t)(v | NAU7802_PUR);
            regs[NAU7802_PU_CTRL] = v;
            return ESP_OK;
        }
        if (reg == NAU7802_CTRL2 && (v & NAU7802_CALS)) {
            cal_runs++;
            cal_polls_left = 3;
            regs[NAU7802_CTRL2] = v;
            return ESP_OK;
        }
        if (reg < sizeof(regs))
            regs[reg] = v;
    }
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d, const uint8_t *tx,
                                      size_t tx_len, uint8_t *rx, size_t rx_len, int t)
{
    (void)d; (void)t;
    assert(tx_len == 1);

    if (tx[0] == NAU7802_CTRL2 && cal_polls_left > 0) {
        if (--cal_polls_left == 0) {
            regs[NAU7802_CTRL2] &= (uint8_t)~NAU7802_CALS;
            if (cal_should_fail)
                regs[NAU7802_CTRL2] |= NAU7802_CAL_ERR;
        }
    }
    if (tx[0] == NAU7802_PU_CTRL) {
        /* Data is only ever ready once conversions were started. */
        if ((regs[NAU7802_PU_CTRL] & NAU7802_CS) && !never_ready) {
            regs[NAU7802_PU_CTRL] |= NAU7802_CR;
            load_sample();
        }
    }
    for (size_t i = 0; i < rx_len; i++)
        rx[i] = regs[(tx[0] + i) & 0x3F];
    return ESP_OK;
}

void vTaskDelay(TickType_t t) { (void)t; }

static nau7802_config_t base_cfg(void)
{
    nau7802_config_t c = { .gain = NAU7802_GAIN_128, .rate = NAU7802_RATE_80SPS,
                           .ldo = NAU7802_LDO_3V3, .use_internal_ldo = true };
    return c;
}

static nau7802_handle_t open_ok(int32_t value)
{
    nau7802_config_t c = base_cfg();
    nau7802_handle_t d = NULL;

    chip_reset(value);
    assert(nau7802_init(NULL, &c, &d) == ESP_OK);
    return d;
}

static void test_startup_sequence(void)
{
    nau7802_config_t c = base_cfg();
    nau7802_handle_t d = NULL;

    chip_reset(0);
    assert(nau7802_init(NULL, &c, &d) == ESP_OK);
    assert(dev_addr == NAU7802_ADDR);
    assert(resets == 1 && "the datasheet sequence starts with RR");
    /* Analog and digital both up, and CS set or nothing ever converts. */
    assert(regs[NAU7802_PU_CTRL] & NAU7802_PUD);
    assert(regs[NAU7802_PU_CTRL] & NAU7802_PUA);
    assert(regs[NAU7802_PU_CTRL] & NAU7802_CS);
    /* The internal LDO was asked for, so AVDDS must be set. */
    assert(regs[NAU7802_PU_CTRL] & NAU7802_AVDDS);
    /* Gain, rate and LDO landed in the right fields. */
    assert((regs[NAU7802_CTRL1] & 0x07) == NAU7802_GAIN_128);
    assert(((regs[NAU7802_CTRL1] >> NAU7802_VLDO_SHIFT) & 0x07) == NAU7802_LDO_3V3);
    assert(((regs[NAU7802_CTRL2] >> NAU7802_CRS_SHIFT) & 0x07) == NAU7802_RATE_80SPS);
    assert(cal_runs == 1 && "init runs the internal offset calibration");
    nau7802_del(d);
    /* Teardown powers both sections down. */
    assert(regs[NAU7802_PU_CTRL] == 0);

    /* Without the internal LDO, AVDDS must stay clear. */
    chip_reset(0);
    c.use_internal_ldo = false;
    assert(nau7802_init(NULL, &c, &d) == ESP_OK);
    assert(!(regs[NAU7802_PU_CTRL] & NAU7802_AVDDS));
    nau7802_del(d);
}

static void test_bad_config(void)
{
    nau7802_config_t c;
    nau7802_handle_t d = NULL;

    chip_reset(0);
    c = base_cfg(); c.gain = (nau7802_gain_t)8;
    assert(nau7802_init(NULL, &c, &d) == ESP_ERR_INVALID_ARG);
    /* Rate codes 4, 5 and 6 do not exist. */
    c = base_cfg(); c.rate = (nau7802_rate_t)4;
    assert(nau7802_init(NULL, &c, &d) == ESP_ERR_INVALID_ARG);
    c = base_cfg(); c.rate = (nau7802_rate_t)6;
    assert(nau7802_init(NULL, &c, &d) == ESP_ERR_INVALID_ARG);
    c = base_cfg(); c.ldo = (nau7802_ldo_t)8;
    assert(nau7802_init(NULL, &c, &d) == ESP_ERR_INVALID_ARG);
    c = base_cfg(); c.channel = 3;
    assert(nau7802_init(NULL, &c, &d) == ESP_ERR_INVALID_ARG);
    assert(nau7802_init(NULL, NULL, &d) == ESP_ERR_INVALID_ARG);
    assert(nau7802_init(NULL, &c, NULL) == ESP_ERR_INVALID_ARG);
}

static void test_calibration_failure_is_reported(void)
{
    nau7802_config_t c = base_cfg();
    nau7802_handle_t d = NULL;

    chip_reset(0);
    cal_should_fail = true;
    /* CAL_ERR must not be ignored: an untrimmed offset is a silent error. */
    assert(nau7802_init(NULL, &c, &d) == ESP_ERR_INVALID_STATE);
}

static void test_read(void)
{
    struct { int32_t v; } cases[] = {
        { 0 }, { 1 }, { -1 }, { 123456 }, { -123456 },
        { NAU7802_COUNT_MAX }, { NAU7802_COUNT_MIN },
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        nau7802_handle_t d = open_ok(cases[i].v);
        int32_t got = 0;

        assert(nau7802_read_raw(d, &got, 100) == ESP_OK);
        assert(got == cases[i].v);
        nau7802_del(d);
    }

    /* Averaging returns the same value when the input does not move. */
    {
        nau7802_handle_t d = open_ok(5000);
        int32_t avg = 0;

        assert(nau7802_read_average(d, 8, &avg, 100) == ESP_OK);
        assert(avg == 5000);
        assert(nau7802_read_average(d, 0, &avg, 100) == ESP_ERR_INVALID_ARG);
        nau7802_del(d);
    }
}

static void test_timeout(void)
{
    nau7802_config_t c = base_cfg();
    nau7802_handle_t d = NULL;
    int32_t v;

    chip_reset(0);
    assert(nau7802_init(NULL, &c, &d) == ESP_OK);
    never_ready = true;
    regs[NAU7802_PU_CTRL] &= (uint8_t)~NAU7802_CR;
    assert(nau7802_read_raw(d, &v, 20) == ESP_ERR_TIMEOUT);
    nau7802_del(d);
}

static void test_gain_channel_recalibrate(void)
{
    nau7802_handle_t d = open_ok(1000);

    cal_runs = 0;
    assert(nau7802_set_gain(d, NAU7802_GAIN_32) == ESP_OK);
    assert((regs[NAU7802_CTRL1] & 0x07) == NAU7802_GAIN_32);
    /* The trimmed offset belonged to the old gain. */
    assert(cal_runs == 1 && "changing gain must re-run the internal calibration");

    cal_runs = 0;
    assert(nau7802_set_channel(d, 2, 100) == ESP_OK);
    assert(regs[NAU7802_CTRL2] & NAU7802_CHS);
    assert(cal_runs == 1);
    assert(nau7802_set_channel(d, 1, 100) == ESP_OK);
    assert(!(regs[NAU7802_CTRL2] & NAU7802_CHS));

    assert(nau7802_set_channel(d, 3, 100) == ESP_ERR_INVALID_ARG);
    assert(nau7802_set_gain(d, (nau7802_gain_t)9) == ESP_ERR_INVALID_ARG);
    assert(nau7802_set_rate(d, (nau7802_rate_t)5) == ESP_ERR_INVALID_ARG);
    assert(nau7802_set_rate(d, NAU7802_RATE_320SPS) == ESP_OK);
    assert(((regs[NAU7802_CTRL2] >> NAU7802_CRS_SHIFT) & 0x07) == 7);

    nau7802_del(d);
}

static void test_weight_calibration(void)
{
    nau7802_handle_t d = open_ok(1000);
    float u = 0.0f;

    assert(nau7802_tare(d, 4, 100) == ESP_OK);
    assert(nau7802_offset(d) == 1000);
    assert(nau7802_read_units(d, 1, &u, 100) == ESP_OK && fabs(u) < 1e-4);

    sample = 3000;
    assert(nau7802_calibrate(d, 2.0f, 4, 100) == ESP_OK);
    assert(fabs(nau7802_scale(d) - 1000.0f) < 1e-3);
    assert(nau7802_read_units(d, 1, &u, 100) == ESP_OK);
    assert(fabs(u - 2.0f) < 1e-3);

    /* Calibrating without the mass on must be refused, and must not destroy
     * the scale that was already good. */
    sample = 1000;
    assert(nau7802_calibrate(d, 2.0f, 4, 100) == ESP_ERR_INVALID_ARG);
    assert(fabs(nau7802_scale(d) - 1000.0f) < 1e-3);

    nau7802_set_scale(d, 0.0f);       /* ignored: would divide by zero later */
    assert(fabs(nau7802_scale(d) - 1000.0f) < 1e-3);
    nau7802_set_offset(d, 42);
    assert(nau7802_offset(d) == 42);

    nau7802_del(d);
}

static void test_saturation_reported(void)
{
    nau7802_handle_t d = open_ok(NAU7802_COUNT_MAX);
    int32_t v;

    assert(nau7802_read_raw(d, &v, 100) == ESP_OK);
    assert(nau7802_last_saturated(d));
    assert(nau7802_tare(d, 2, 100) == ESP_ERR_INVALID_STATE);
    nau7802_del(d);
}

static void test_null_arguments(void)
{
    nau7802_handle_t d = NULL;
    int32_t v;
    bool b;
    float f;
    uint8_t u;

    assert(nau7802_read_raw(NULL, &v, 100) == ESP_ERR_INVALID_ARG);
    assert(nau7802_read_average(NULL, 1, &v, 100) == ESP_ERR_INVALID_ARG);
    assert(nau7802_is_ready(NULL, &b) == ESP_ERR_INVALID_ARG);
    assert(nau7802_read_units(NULL, 1, &f, 100) == ESP_ERR_INVALID_ARG);
    assert(nau7802_tare(NULL, 1, 100) == ESP_ERR_INVALID_ARG);
    assert(nau7802_calibrate(NULL, 1.0f, 1, 100) == ESP_ERR_INVALID_ARG);
    assert(nau7802_calibrate_internal(NULL) == ESP_ERR_INVALID_ARG);
    assert(nau7802_set_gain(NULL, NAU7802_GAIN_1) == ESP_ERR_INVALID_ARG);
    assert(nau7802_set_rate(NULL, NAU7802_RATE_10SPS) == ESP_ERR_INVALID_ARG);
    assert(nau7802_set_channel(NULL, 1, 100) == ESP_ERR_INVALID_ARG);
    assert(nau7802_sleep(NULL, true) == ESP_ERR_INVALID_ARG);
    assert(nau7802_del(NULL) == ESP_ERR_INVALID_ARG);
    assert(nau7802_reg_read(NULL, 0, &u, 1) == ESP_ERR_INVALID_ARG);
    assert(nau7802_reg_write(NULL, 0, 0) == ESP_ERR_INVALID_ARG);
    assert(nau7802_offset(NULL) == 0 && nau7802_scale(NULL) == 0.0f);
    assert(!nau7802_last_saturated(NULL));

    d = open_ok(0);
    assert(nau7802_read_raw(d, NULL, 100) == ESP_ERR_INVALID_ARG);
    assert(nau7802_is_ready(d, NULL) == ESP_ERR_INVALID_ARG);
    assert(nau7802_reg_read(d, 0, &u, 0) == ESP_ERR_INVALID_ARG);
    nau7802_del(d);
}

int main(void)
{
    test_startup_sequence();
    test_bad_config();
    test_calibration_failure_is_reported();
    test_read();
    test_timeout();
    test_gain_channel_recalibrate();
    test_weight_calibration();
    test_saturation_reported();
    test_null_arguments();
    printf("all nau7802 driver checks passed\n");
    return 0;
}
