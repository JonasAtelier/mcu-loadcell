/*
 * Host-side check of the driver against a fake HX711.
 *
 * The fake watches the clock line the way the real chip does: it counts
 * pulses, hands out one bit per pulse, and -- crucially -- notices if the
 * clock is ever left high long enough to trigger a power-down. That is the
 * failure this part is famous for on an RTOS, so the test asserts a normal
 * read never causes one.
 */
#define HX711_HOST_TEST 1

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hx711.h"
#include "hx711_math.h"
#include "hx711_proto.h"

#define DOUT_PIN 4
#define SCK_PIN  5

static int      sck_level, dout_level;
static uint32_t sck_high_us;          /* how long the clock has been high */
static int      pulses;               /* pulses in the current frame */
static uint32_t pending;              /* the 24-bit word being shifted out */
static int      bits_left;
static int      trailing_seen;        /* pulses after the 24 data bits */
static int      power_downs, frames;
static bool     powered_down;
static bool     just_pulsed;   /* a DOUT read straight after a clock edge */
static int      last_trailing;
static bool     device_absent;
static int32_t  next_value;

static void load_frame(void)
{
    pending = (uint32_t)next_value & 0x00FFFFFFu;
    bits_left = HX711_DATA_BITS;
    trailing_seen = 0;
    dout_level = device_absent ? 1 : 0;    /* low means ready */
}

static void fake_reset(int32_t value)
{
    sck_level = 0; sck_high_us = 0; pulses = 0; powered_down = false;
    power_downs = frames = last_trailing = 0;
    device_absent = false;
    next_value = value;
    load_frame();
}

esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return ESP_OK; }

esp_err_t gpio_set_level(gpio_num_t g, uint32_t level)
{
    if (g != SCK_PIN)
        return ESP_OK;

    if (device_absent) {
        /* Nothing on the wire: DOUT is pulled high and never moves, whatever
         * the clock does. */
        sck_level = (int)level;
        dout_level = 1;
        return ESP_OK;
    }

    if (level && !sck_level) {
        sck_high_us = 0;                    /* rising edge */
    } else if (!level && sck_level) {
        /* The falling edge that ends a power-down wakes the chip; it does not
         * clock data out. Modelling this matters, because the driver's
         * power-up does exactly that. */
        if (powered_down) {
            powered_down = false;
            load_frame();
            sck_level = 0;
            return ESP_OK;
        }
        /* falling edge completes a pulse */
        if (bits_left > 0) {
            dout_level = (pending >> (bits_left - 1)) & 1;
            bits_left--;
        } else {
            trailing_seen++;
            if (trailing_seen == 1)
                dout_level = 1;             /* DOUT goes high after the frame */
        }
        pulses++;
        just_pulsed = true;
    }
    sck_level = (int)level;
    return ESP_OK;
}

int gpio_get_level(gpio_num_t g)
{
    if (g != DOUT_PIN)
        return sck_level;

    if (just_pulsed) {
        /* the driver sampling the bit it just clocked out */
        just_pulsed = false;
    } else if (!powered_down && bits_left == 0 && trailing_seen > 0) {
        /* the clock has gone idle and the frame is finished, so the chip has
         * had time to finish another conversion */
        frames++;
        load_frame();
    }
    return dout_level;
}

void esp_rom_delay_us(uint32_t us)
{
    if (sck_level && !device_absent) {
        sck_high_us += us;
        if (sck_high_us >= 60 && !powered_down) {  /* datasheet reset threshold */
            power_downs++;
            last_trailing = trailing_seen;
            frames++;
            powered_down = true;            /* stays down until SCK falls */
        }
    }
}

void vTaskDelay(TickType_t t) { (void)t; }

/* -------------------------------------------------------------- tests ---- */

static hx711_config_t base_cfg(void)
{
    hx711_config_t c = { .dout_gpio = DOUT_PIN, .sck_gpio = SCK_PIN,
                         .gain = HX711_GAIN_A128 };
    return c;
}

static hx711_handle_t open_ok(int32_t value)
{
    hx711_config_t c = base_cfg();
    hx711_handle_t d = NULL;

    fake_reset(value);
    assert(hx711_init(&c, &d) == ESP_OK);
    assert(d);
    return d;
}

static void test_bad_config(void)
{
    hx711_config_t c = base_cfg();
    hx711_handle_t d = NULL;

    fake_reset(0);
    c.gain = (hx711_gain_t)100;
    assert(hx711_init(&c, &d) == ESP_ERR_INVALID_ARG);
    c = base_cfg(); c.dout_gpio = -1;
    assert(hx711_init(&c, &d) == ESP_ERR_INVALID_ARG);
    c = base_cfg(); c.sck_gpio = -1;
    assert(hx711_init(&c, &d) == ESP_ERR_INVALID_ARG);
    /* Both pins the same is a wiring mistake, not a configuration. */
    c = base_cfg(); c.sck_gpio = c.dout_gpio;
    assert(hx711_init(&c, &d) == ESP_ERR_INVALID_ARG);
    assert(hx711_init(NULL, &d) == ESP_ERR_INVALID_ARG);
    assert(hx711_init(&c, NULL) == ESP_ERR_INVALID_ARG);
}

static void test_absent_device(void)
{
    hx711_config_t c = base_cfg();
    hx711_handle_t d = NULL;

    /* Nothing wired: DOUT stays high forever. Must time out, not hang or
     * return a number. */
    fake_reset(0);
    device_absent = true;
    dout_level = 1;
    c.gain = HX711_GAIN_B32;         /* forces a throwaway read during init */
    assert(hx711_init(&c, &d) == ESP_ERR_TIMEOUT);
}

static void test_read_and_sign(void)
{
    struct { int32_t v; } cases[] = {
        { 0 }, { 1 }, { -1 }, { 12345 }, { -12345 },
        { HX711_COUNT_MAX }, { HX711_COUNT_MIN },
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        hx711_handle_t d = open_ok(cases[i].v);
        int32_t got = 0;

        assert(hx711_read_raw(d, &got, 100) == ESP_OK);
        assert(got == cases[i].v);
        hx711_del(d);
    }
}

static void test_clock_never_resets_the_part(void)
{
    hx711_handle_t d = open_ok(4242);
    int32_t v;

    /* A normal read must not hold the clock high anywhere near 60 us. This is
     * the whole reason the driver has a critical section. */
    power_downs = 0;
    assert(hx711_read_raw(d, &v, 100) == ESP_OK);
    assert(v == 4242);
    assert(power_downs == 0 && "a read must never power the chip down");

    assert(hx711_read_average(d, 10, &v, 100) == ESP_OK);
    assert(v == 4242);
    assert(power_downs == 0);

    hx711_del(d);
}

static void test_gain_pulses_reach_the_wire(void)
{
    hx711_config_t c = base_cfg();
    hx711_handle_t d = NULL;
    int32_t v;

    /* Gain 128 sends one trailing pulse. */
    fake_reset(100);
    c.gain = HX711_GAIN_A128;
    assert(hx711_init(&c, &d) == ESP_OK);
    /* trailing_seen is still the count for the frame just read: the driver
     * does not touch DOUT again until the next wait, which is what reloads. */
    assert(hx711_read_raw(d, &v, 100) == ESP_OK);
    assert(trailing_seen == HX711_PULSES_A_128);
    hx711_del(d);

    /* Channel B at gain 32 sends two. */
    fake_reset(100);
    c.gain = HX711_GAIN_B32;
    assert(hx711_init(&c, &d) == ESP_OK);
    assert(hx711_read_raw(d, &v, 100) == ESP_OK);
    assert(trailing_seen == HX711_PULSES_B_32);

    /* Switching gain at runtime changes the pulse count. */
    assert(hx711_set_gain(d, HX711_GAIN_A64, 100) == ESP_OK);
    assert(hx711_read_raw(d, &v, 100) == ESP_OK);
    assert(trailing_seen == HX711_PULSES_A_64);

    assert(hx711_set_gain(d, (hx711_gain_t)7, 100) == ESP_ERR_INVALID_ARG);
    hx711_del(d);
}

static void test_calibration(void)
{
    hx711_handle_t d = open_ok(1000);
    float u = 0.0f;

    /* Tare with nothing on it. */
    assert(hx711_tare(d, 4, 100) == ESP_OK);
    assert(hx711_offset(d) == 1000);
    assert(hx711_read_units(d, 1, &u, 100) == ESP_OK);
    assert(fabs(u) < 1e-4);

    /* Put 2 units on: the reading moves to 3000, so 1000 counts per unit. */
    next_value = 3000; load_frame();
    assert(hx711_calibrate(d, 2.0f, 4, 100) == ESP_OK);
    assert(fabs(hx711_scale(d) - 1000.0f) < 1e-3);
    assert(hx711_read_units(d, 1, &u, 100) == ESP_OK);
    assert(fabs(u - 2.0f) < 1e-3);

    /* Half the load reads half the weight. */
    next_value = 2000; load_frame();
    assert(hx711_read_units(d, 1, &u, 100) == ESP_OK);
    assert(fabs(u - 1.0f) < 1e-3);

    /* Calibrating with the mass not actually on it must be refused. */
    next_value = 1000; load_frame();
    assert(hx711_calibrate(d, 2.0f, 4, 100) == ESP_ERR_INVALID_ARG);
    /* ...and must not have destroyed the good scale. */
    assert(fabs(hx711_scale(d) - 1000.0f) < 1e-3);

    assert(hx711_calibrate(d, 0.0f, 4, 100) == ESP_ERR_INVALID_ARG);
    assert(hx711_tare(d, 0, 100) == ESP_ERR_INVALID_ARG);
    assert(hx711_read_average(d, 0, NULL, 100) == ESP_ERR_INVALID_ARG);

    hx711_set_offset(d, 55);
    assert(hx711_offset(d) == 55);
    hx711_set_scale(d, 7.5f);
    assert(fabs(hx711_scale(d) - 7.5f) < 1e-6);
    /* A zero scale would divide by zero later, so it is ignored. */
    hx711_set_scale(d, 0.0f);
    assert(fabs(hx711_scale(d) - 7.5f) < 1e-6);

    hx711_del(d);
}

static void test_saturation_reported(void)
{
    hx711_handle_t d = open_ok(HX711_COUNT_MAX);
    int32_t v;

    assert(hx711_read_raw(d, &v, 100) == ESP_OK);
    assert(hx711_last_saturated(d) && "a pinned reading is not a measurement");
    /* Taring against a saturated cell must be refused. */
    assert(hx711_tare(d, 2, 100) == ESP_ERR_INVALID_STATE);
    hx711_del(d);

    d = open_ok(500);
    assert(hx711_read_raw(d, &v, 100) == ESP_OK);
    assert(!hx711_last_saturated(d));
    assert(hx711_tare(d, 2, 100) == ESP_OK);
    hx711_del(d);
}

static void test_power_and_nulls(void)
{
    hx711_handle_t d = open_ok(777);
    bool ready = false;
    int32_t v;

    assert(hx711_is_ready(d, &ready) == ESP_OK);
    power_downs = 0;
    assert(hx711_power_down(d) == ESP_OK);
    assert(power_downs >= 1 && "power down holds the clock high past 60 us");
    assert(hx711_power_up(d) == ESP_OK);

    assert(hx711_read_raw(NULL, &v, 100) == ESP_ERR_INVALID_ARG);
    assert(hx711_read_raw(d, NULL, 100) == ESP_ERR_INVALID_ARG);
    assert(hx711_is_ready(NULL, &ready) == ESP_ERR_INVALID_ARG);
    assert(hx711_is_ready(d, NULL) == ESP_ERR_INVALID_ARG);
    assert(hx711_read_units(NULL, 1, NULL, 100) == ESP_ERR_INVALID_ARG);
    assert(hx711_power_down(NULL) == ESP_ERR_INVALID_ARG);
    assert(hx711_power_up(NULL) == ESP_ERR_INVALID_ARG);
    assert(hx711_set_gain(NULL, HX711_GAIN_A128, 100) == ESP_ERR_INVALID_ARG);
    assert(hx711_del(NULL) == ESP_ERR_INVALID_ARG);
    assert(hx711_offset(NULL) == 0);
    assert(hx711_scale(NULL) == 0.0f);
    assert(!hx711_last_saturated(NULL));

    hx711_del(d);
}

int main(void)
{
    test_bad_config();
    test_absent_device();
    test_read_and_sign();
    test_clock_never_resets_the_part();
    test_gain_pulses_reach_the_wire();
    test_calibration();
    test_saturation_reported();
    test_power_and_nulls();
    printf("all hx711 driver checks passed\n");
    return 0;
}
