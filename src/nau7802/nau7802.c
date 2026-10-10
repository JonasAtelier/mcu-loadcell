/* First: on ESP-IDF it brings in sdkconfig.h, which the #if below reads. */
#include "loadcell_priv.h"

#if CONFIG_LOADCELL_NAU7802

#include <stdlib.h>
#include <string.h>

#include "loadcell_log.h"
#include "nau7802.h"
#include "nau7802_math.h"
#include "nau7802_priv.h"
#include "nau7802_regs.h"

#define DEFAULT_SCL_HZ  400000
#define I2C_TIMEOUT_MS  100
#define POWERUP_MS      200     /* the original's 100 tries of 2 ms; PUR is
                                 * "about 200 microseconds" (9.1, p.24) */
#define CAL_MS          400     /* the original's 200 tries of 2 ms */
#define POLL_MS         2

#define TRY(expr) do { int _e = (expr); if (_e) return _e; } while (0)

/* Who logs before there is a handle to log as. */
static const struct loadcell_dev unbound = { .name = "nau7802" };

/* ---------------------------------------------------------------- I2C ---- */

static int reg_read(nau7802_handle_t d, uint8_t reg, void *buf, size_t len)
{
    return d->bus.ops->transfer(&d->bus, reg, NULL, 0, buf, len);
}

static int reg_write(nau7802_handle_t d, uint8_t reg, uint8_t value)
{
    return d->bus.ops->transfer(&d->bus, reg, &value, 1, NULL, 0);
}

static int reg_update(nau7802_handle_t d, uint8_t reg, uint8_t mask,
                      uint8_t value)
{
    uint8_t v = 0;

    TRY(reg_read(d, reg, &v, 1));
    v = (uint8_t)((v & ~mask) | (value & mask));
    return reg_write(d, reg, v);
}

/* -------------------------------------------------------- calibration ---- */

/*
 * The chip's own offset trim. Section 11.3: writing CALS starts it, the bit
 * reads 1 while it runs, and CAL_ERR reports the outcome. Worth doing at
 * startup and after any gain or channel change, because the offset it trims
 * depends on both.
 */
static int calibrate_internal(nau7802_handle_t d)
{
    uint32_t start;
    uint8_t v = 0;

    TRY(reg_update(d, NAU7802_CTRL2, NAU7802_CALMOD_MASK,
                   NAU7802_CALMOD_INTERNAL_OFFSET));
    TRY(reg_update(d, NAU7802_CTRL2, NAU7802_CALS, NAU7802_CALS));

    start = loadcell_tick_now();
    for (;;) {
        TRY(reg_read(d, NAU7802_CTRL2, &v, 1));
        if (!(v & NAU7802_CALS)) {
            if (v & NAU7802_CAL_ERR) {
                loadcell_err(&d->cell,
                             "internal calibration reported an error");
                return -EPROTO;
            }
            return 0;
        }
        if (loadcell_expired(start, CAL_MS))
            return -ETIMEDOUT;
        loadcell_poll_wait(POLL_MS);
    }
}

/* ------------------------------------------------------------- reading --- */

static int is_ready(nau7802_handle_t d, bool *ready)
{
    uint8_t v = 0;

    TRY(reg_read(d, NAU7802_PU_CTRL, &v, 1));
    *ready = (v & NAU7802_CR) != 0;
    return 0;
}

/* The deadline is counted in ticks, at least one per poll; see
 * loadcell_poll_wait(). */
static int read_raw(nau7802_handle_t d, int32_t *value, uint32_t timeout_ms)
{
    uint8_t b[NAU7802_ADC_BYTES];
    uint32_t start = loadcell_tick_now();

    for (;;) {
        bool ready = false;

        TRY(is_ready(d, &ready));
        if (ready)
            break;
        if (loadcell_expired(start, timeout_ms))
            return -ETIMEDOUT;
        loadcell_poll_wait(POLL_MS);
    }

    TRY(reg_read(d, NAU7802_ADCO_B2, b, sizeof(b)));
    *value = nau7802_raw(b);
    d->cell.saturated = nau7802_saturated(*value);
    return 0;
}

/* ------------------------------------------------- the loadcell_dev ops -- */

static nau7802_handle_t of(struct loadcell_dev *cell)
{
    return cell->priv;
}

static int op_is_ready(struct loadcell_dev *cell, bool *ready)
{
    return is_ready(of(cell), ready);
}

static int op_read_raw(struct loadcell_dev *cell, int32_t *counts,
                       uint32_t timeout_ms)
{
    return read_raw(of(cell), counts, timeout_ms);
}

static const struct loadcell_ops nau7802_ops = {
    .is_ready = op_is_ready,
    .read_raw = op_read_raw,
};

/* -------------------------------------------------------------- init ----- */

static int set_gain(nau7802_handle_t d, nau7802_gain_t gain)
{
    if (nau7802_gain_value(gain) == 0 || gain > NAU7802_GAIN_128)
        return -EINVAL;

    TRY(reg_update(d, NAU7802_CTRL1, 0x07, (uint8_t)gain));
    d->gain = gain;
    /* The trimmed offset belongs to the old gain. */
    return calibrate_internal(d);
}

static int set_channel(nau7802_handle_t d, int channel, uint32_t timeout_ms)
{
    int32_t discard;

    if (channel != 1 && channel != 2)
        return -EINVAL;

    TRY(reg_update(d, NAU7802_CTRL2, NAU7802_CHS,
                   channel == 2 ? NAU7802_CHS : 0));
    d->channel = channel;
    TRY(calibrate_internal(d));

    /* The conversion in flight belongs to the old channel. */
    return read_raw(d, &discard, timeout_ms);
}

static int init(loadcell_i2c_bus bus, const nau7802_config_t *cfg,
                nau7802_handle_t *out)
{
    nau7802_handle_t d;
    uint32_t start;
    uint8_t v = 0;
    int channel;
    int err;

    if (!cfg || !out)
        return -EINVAL;
#ifndef ESP_PLATFORM
    if (!bus)
        return -EINVAL;
#endif

    channel = cfg->channel ? cfg->channel : 1;
    if (channel != 1 && channel != 2)
        return -EINVAL;
    if (nau7802_gain_value(cfg->gain) == 0 || cfg->gain > NAU7802_GAIN_128 ||
        nau7802_rate_sps(cfg->rate) == 0 ||
        nau7802_ldo_mv(cfg->ldo) == 0)
        return -EINVAL;

    /* Presence first, before anything is allocated or written: a NACK on a
     * transfer would only be a plain I/O error. */
    err = loadcell_i2c_probe(bus, NAU7802_ADDR, I2C_TIMEOUT_MS);
    if (err) {
        if (err == -ENXIO)
            loadcell_err(&unbound, "nothing answers at 0x%02x", NAU7802_ADDR);
        return err;
    }

    d = calloc(1, sizeof(*d));
    if (!d)
        return -ENOMEM;

    d->cell.name  = "nau7802";
    d->gain       = cfg->gain;
    d->rate       = cfg->rate;
    d->channel    = channel;
    d->cell.scale = 1.0f;

    err = loadcell_i2c_attach(&d->bus, bus, NAU7802_ADDR,
                              cfg->scl_hz ? cfg->scl_hz : DEFAULT_SCL_HZ,
                              I2C_TIMEOUT_MS);
    if (err) {
        free(d);
        return err;
    }

    /* Section 9.1 start-up sequence: RR high then low, then PUD, then wait
     * for PUR before touching anything else. */
    err = reg_write(d, NAU7802_PU_CTRL, NAU7802_RR);
    if (!err)
        err = reg_write(d, NAU7802_PU_CTRL, NAU7802_PUD);
    if (err)
        goto fail;

    start = loadcell_tick_now();
    for (;;) {
        err = reg_read(d, NAU7802_PU_CTRL, &v, 1);
        if (err)
            goto fail;
        if (v & NAU7802_PUR)
            break;
        if (loadcell_expired(start, POWERUP_MS)) {
            loadcell_err(&d->cell,
                         "no NAU7802 at 0x%02x: never reported power-up ready",
                         NAU7802_ADDR);
            err = -ENODEV;
            goto fail;
        }
        loadcell_poll_wait(POLL_MS);
    }

    /* Analog on, and the LDO only if asked -- AVDDS selects between the
     * internal regulator and whatever is on the AVDD pin. */
    v = (uint8_t)(NAU7802_PUD | NAU7802_PUA);
    if (cfg->use_internal_ldo)
        v |= NAU7802_AVDDS;
    err = reg_write(d, NAU7802_PU_CTRL, v);
    if (err)
        goto fail;

    err = reg_write(d, NAU7802_CTRL1,
                    (uint8_t)(((uint8_t)cfg->ldo << NAU7802_VLDO_SHIFT) |
                              (uint8_t)cfg->gain));
    if (!err)
        err = reg_write(d, NAU7802_CTRL2,
                        (uint8_t)(((uint8_t)cfg->rate << NAU7802_CRS_SHIFT) |
                                  (channel == 2 ? NAU7802_CHS : 0)));
    if (err)
        goto fail;

    err = calibrate_internal(d);
    if (err)
        goto fail;

    /* Nothing converts until CS is set. */
    err = reg_update(d, NAU7802_PU_CTRL, NAU7802_CS, NAU7802_CS);
    if (err)
        goto fail;

    loadcell_info(&d->cell, "NAU7802 up: channel %d, gain x%d, %d SPS%s",
                  d->channel, nau7802_gain_value(d->gain),
                  nau7802_rate_sps(d->rate),
                  cfg->use_internal_ldo ? ", internal LDO" : "");

    d->cell.ops  = &nau7802_ops;
    d->cell.priv = d;
    *out = d;
    return 0;

fail:
    loadcell_bus_detach(&d->bus);
    free(d);
    return err;
}

/* ------------------------------------------------------ the handle API -- */

loadcell_handle_ret nau7802_init(loadcell_i2c_bus bus,
                                 const nau7802_config_t *cfg,
                                 nau7802_handle_t *out)
{
    return LOADCELL_HANDLE_RET(init(bus, cfg, out));
}

loadcell_handle_ret nau7802_del(nau7802_handle_t d)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);

    reg_write(d, NAU7802_PU_CTRL, 0);    /* both sections down */
    loadcell_bus_detach(&d->bus);
    free(d);
    return LOADCELL_HANDLE_RET(0);
}

loadcell_handle_ret nau7802_reg_read(nau7802_handle_t d, uint8_t reg, void *buf,
                                     size_t len)
{
    if (!d || !buf || len == 0)
        return LOADCELL_HANDLE_RET(-EINVAL);

    return LOADCELL_HANDLE_RET(reg_read(d, reg, buf, len));
}

loadcell_handle_ret nau7802_reg_write(nau7802_handle_t d, uint8_t reg,
                                      uint8_t value)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);

    return LOADCELL_HANDLE_RET(reg_write(d, reg, value));
}

loadcell_handle_ret nau7802_calibrate_internal(nau7802_handle_t d)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);

    return LOADCELL_HANDLE_RET(calibrate_internal(d));
}

loadcell_handle_ret nau7802_set_gain(nau7802_handle_t d, nau7802_gain_t gain)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);

    return LOADCELL_HANDLE_RET(set_gain(d, gain));
}

loadcell_handle_ret nau7802_set_rate(nau7802_handle_t d, nau7802_rate_t rate)
{
    int err;

    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);
    if (nau7802_rate_sps(rate) == 0)
        return LOADCELL_HANDLE_RET(-EINVAL);

    err = reg_update(d, NAU7802_CTRL2, 0x70,
                     (uint8_t)((uint8_t)rate << NAU7802_CRS_SHIFT));
    if (!err)
        d->rate = rate;
    return LOADCELL_HANDLE_RET(err);
}

loadcell_handle_ret nau7802_set_channel(nau7802_handle_t d, int channel,
                                        uint32_t timeout_ms)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);

    return LOADCELL_HANDLE_RET(set_channel(d, channel, timeout_ms));
}

loadcell_handle_ret nau7802_is_ready(nau7802_handle_t d, bool *ready)
{
    if (!d || !ready)
        return LOADCELL_HANDLE_RET(-EINVAL);

    return LOADCELL_HANDLE_RET(is_ready(d, ready));
}

loadcell_handle_ret nau7802_read_raw(nau7802_handle_t d, int32_t *value,
                                     uint32_t timeout_ms)
{
    if (!d || !value)
        return LOADCELL_HANDLE_RET(-EINVAL);

    return LOADCELL_HANDLE_RET(read_raw(d, value, timeout_ms));
}

loadcell_handle_ret nau7802_read_average(nau7802_handle_t d, int n,
                                         int32_t *value, uint32_t timeout_ms)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);
    return LOADCELL_HANDLE_RET(loadcell_read_average(&d->cell, n, value,
                                                     timeout_ms));
}

bool nau7802_last_saturated(nau7802_handle_t d)
{
    return d ? loadcell_last_saturated(&d->cell) : false;
}

loadcell_handle_ret nau7802_sleep(nau7802_handle_t d, bool sleep)
{
    uint8_t up = (uint8_t)(NAU7802_PUA | NAU7802_PUD);

    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);

    /* PUA and PUD both down is the standby state, 9.1 step 6 (p.24). */
    return LOADCELL_HANDLE_RET(reg_update(d, NAU7802_PU_CTRL, up,
                                          sleep ? 0 : up));
}

/* ---------------------------------------------------- weight calibration - */

loadcell_handle_ret nau7802_tare(nau7802_handle_t d, int n, uint32_t timeout_ms)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);
    return LOADCELL_HANDLE_RET(loadcell_tare(&d->cell, n, timeout_ms));
}

loadcell_handle_ret nau7802_calibrate(nau7802_handle_t d, float known_weight,
                                      int n, uint32_t timeout_ms)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);
    return LOADCELL_HANDLE_RET(loadcell_calibrate(&d->cell, known_weight, n,
                                                  timeout_ms));
}

loadcell_handle_ret nau7802_read_units(nau7802_handle_t d, int n, float *units,
                                       uint32_t timeout_ms)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);
    return LOADCELL_HANDLE_RET(loadcell_read_units(&d->cell, n, units,
                                                   timeout_ms));
}

void nau7802_set_offset(nau7802_handle_t d, int32_t offset)
{
    if (d)
        loadcell_set_offset(&d->cell, offset);
}

void nau7802_set_scale(nau7802_handle_t d, float counts_per_unit)
{
    if (d)
        loadcell_set_scale(&d->cell, counts_per_unit);
}

int32_t nau7802_offset(nau7802_handle_t d) { return d ? d->cell.offset : 0; }
float nau7802_scale(nau7802_handle_t d) { return d ? d->cell.scale : 0.0f; }

struct loadcell_dev *nau7802_loadcell(nau7802_handle_t d)
{
    return d ? &d->cell : NULL;
}

#endif /* CONFIG_LOADCELL_NAU7802 */
