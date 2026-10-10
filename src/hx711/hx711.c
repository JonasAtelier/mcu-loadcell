/* First: on ESP-IDF it brings in sdkconfig.h, which the #if below reads. */
#include "loadcell_priv.h"

#if CONFIG_LOADCELL_HX711

#include <stdlib.h>
#include <string.h>

#include "loadcell_log.h"
#include "hx711.h"
#include "hx711_math.h"
#include "hx711_priv.h"
#include "hx711_proto.h"

#define READY_POLL_MS   2
#define DEFAULT_TIMEOUT 1000

#define TRY(expr) do { int _e = (expr); if (_e) return _e; } while (0)

/* Who logs before there is a handle to log as. */
static const struct loadcell_dev unbound = { .name = "hx711" };

/* ------------------------------------------------------------ the wire --- */

/*
 * One clock pulse, returning the bit the chip presented.
 *
 * The masked section covers only the high phase, and that is the whole
 * point: the HX711 powers itself down if the clock stays high for 60 us, so a
 * task switch or an interrupt between raising and lowering the clock silently
 * resets the part mid-reading. Interrupts are free to run between pulses,
 * while the clock is low, which is why this is per-pulse rather than around
 * the whole transfer -- the same choice the Linux driver makes, and it keeps
 * interrupts masked for about two microseconds at a time instead of eighty.
 */
static int hx711_pulse(hx711_handle_t d)
{
    uint32_t irq;
    int bit;

    irq = loadcell_irq_off();
    loadcell_pin_set(&d->sck, true);
    loadcell_delay_us(HX711_CLOCK_HALF_US);
    loadcell_pin_set(&d->sck, false);
    loadcell_irq_restore(irq);

    loadcell_delay_us(HX711_CLOCK_HALF_US);
    /* Sample as late as possible: parasitic capacitance stretches the time
     * DOUT takes to settle after the falling edge. */
    bit = loadcell_pin_get(&d->dout);
    return bit;
}

/*
 * DOUT low means a conversion is waiting. The deadline is counted in ticks,
 * at least one per poll, not in assumed milliseconds per poll; see
 * loadcell_poll_wait().
 */
static int wait_ready(hx711_handle_t d, uint32_t timeout_ms)
{
    uint32_t start = loadcell_tick_now();

    while (loadcell_pin_get(&d->dout)) {
        if (loadcell_expired(start, timeout_ms))
            return -ETIMEDOUT;
        loadcell_poll_wait(READY_POLL_MS);
    }
    return 0;
}

/* Clock out 24 bits, then the trailing pulses that select the next gain. */
static int shift_in(hx711_handle_t d, int32_t *value, uint32_t timeout_ms)
{
    uint32_t raw = 0;
    int pulses = hx711_gain_pulses(d->gain);

    if (pulses == 0)
        return -EINVAL;

    TRY(wait_ready(d, timeout_ms));

    for (int i = 0; i < HX711_DATA_BITS; i++)
        raw = (raw << 1) | (uint32_t)hx711_pulse(d);

    for (int i = 0; i < pulses; i++)
        hx711_pulse(d);

    *value = hx711_sext24(raw);
    d->cell.saturated = hx711_saturated(*value);
    return 0;
}

static void power_down(hx711_handle_t d)
{
    loadcell_pin_set(&d->sck, false);
    loadcell_pin_set(&d->sck, true);
    loadcell_delay_us(HX711_POWER_DOWN_US);   /* over 60 us high is the request */
}

static int power_up(hx711_handle_t d)
{
    int32_t discard;

    loadcell_pin_set(&d->sck, false);
    loadcell_delay_us(HX711_POWER_UP_US);

    /* Waking resets the part to channel A gain 128. One throwaway conversion
     * puts the configured gain back in force. */
    if (d->gain != HX711_RESET_GAIN)
        return shift_in(d, &discard, DEFAULT_TIMEOUT);
    return 0;
}

/* ------------------------------------------------- the loadcell_dev ops -- */

static hx711_handle_t of(struct loadcell_dev *cell)
{
    return cell->priv;
}

static int op_is_ready(struct loadcell_dev *cell, bool *ready)
{
    *ready = !loadcell_pin_get(&of(cell)->dout);
    return 0;
}

static int op_read_raw(struct loadcell_dev *cell, int32_t *counts,
                       uint32_t timeout_ms)
{
    return shift_in(of(cell), counts, timeout_ms);
}

static const struct loadcell_ops hx711_ops = {
    .is_ready = op_is_ready,
    .read_raw = op_read_raw,
};

/* -------------------------------------------------------------- init ----- */

loadcell_handle_ret hx711_power_down(hx711_handle_t d)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);

    power_down(d);
    return LOADCELL_HANDLE_RET(0);
}

loadcell_handle_ret hx711_power_up(hx711_handle_t d)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);

    return LOADCELL_HANDLE_RET(power_up(d));
}

loadcell_handle_ret hx711_set_gain(hx711_handle_t d, hx711_gain_t gain,
                                   uint32_t timeout_ms)
{
    int32_t discard;

    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);
    if (hx711_gain_pulses(gain) == 0)
        return LOADCELL_HANDLE_RET(-EINVAL);

    d->gain = gain;
    /* The trailing pulses configure the NEXT conversion, so one reading has to
     * pass before the new gain is what you are actually measuring at. */
    return LOADCELL_HANDLE_RET(shift_in(d, &discard, timeout_ms));
}

static int init(const hx711_config_t *cfg, hx711_handle_t *out)
{
    hx711_handle_t d;
    int err;

    if (!cfg || !out)
        return -EINVAL;
#ifdef ESP_PLATFORM
    if (cfg->dout_gpio < 0 || cfg->sck_gpio < 0 ||
        cfg->dout_gpio == cfg->sck_gpio)
        return -EINVAL;
#else
    if (!cfg->dout_port || !cfg->sck_port ||
        (cfg->dout_port == cfg->sck_port && cfg->dout_pin == cfg->sck_pin))
        return -EINVAL;
#endif
    if (hx711_gain_pulses(cfg->gain) == 0) {
        loadcell_err(&unbound, "gain must be 128, 64 or 32, not %d",
                     (int)cfg->gain);
        return -EINVAL;
    }

    d = calloc(1, sizeof(*d));
    if (!d)
        return -ENOMEM;

    d->cell.name  = "hx711";
#ifdef ESP_PLATFORM
    d->dout.gpio  = cfg->dout_gpio;
    d->sck.gpio   = cfg->sck_gpio;
#else
    d->dout.port  = cfg->dout_port;
    d->dout.pin   = cfg->dout_pin;
    d->sck.port   = cfg->sck_port;
    d->sck.pin    = cfg->sck_pin;
#endif
    d->gain       = cfg->gain;
    d->cell.scale = 1.0f;

    err = loadcell_pin_config(&d->dout, false);
    if (!err)
        err = loadcell_pin_config(&d->sck, true);
    if (err) {
        free(d);
        return err;
    }

    /* Power cycle so the part starts from a known state, then one throwaway
     * conversion to make the configured gain current. */
    power_down(d);
    err = power_up(d);
    if (err) {
        loadcell_err(&d->cell, "no HX711 responding: DOUT never went low. "
                               "Check power, and that DOUT and SCK are not "
                               "swapped");
        free(d);
        return err;
    }

    loadcell_info(&d->cell, "HX711 up: channel %c, gain %d",
                  hx711_gain_channel(d->gain), d->gain);

    d->cell.ops  = &hx711_ops;
    d->cell.priv = d;
    *out = d;
    return 0;
}

/* ------------------------------------------------------ the handle API -- */

loadcell_handle_ret hx711_init(const hx711_config_t *cfg, hx711_handle_t *out)
{
    return LOADCELL_HANDLE_RET(init(cfg, out));
}

loadcell_handle_ret hx711_del(hx711_handle_t d)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);

    power_down(d);
    free(d);
    return LOADCELL_HANDLE_RET(0);
}

loadcell_handle_ret hx711_is_ready(hx711_handle_t d, bool *ready)
{
    if (!d || !ready)
        return LOADCELL_HANDLE_RET(-EINVAL);

    return LOADCELL_HANDLE_RET(op_is_ready(&d->cell, ready));
}

loadcell_handle_ret hx711_read_raw(hx711_handle_t d, int32_t *value,
                                   uint32_t timeout_ms)
{
    if (!d || !value)
        return LOADCELL_HANDLE_RET(-EINVAL);

    return LOADCELL_HANDLE_RET(shift_in(d, value, timeout_ms));
}

loadcell_handle_ret hx711_read_average(hx711_handle_t d, int n, int32_t *value,
                                       uint32_t timeout_ms)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);
    return LOADCELL_HANDLE_RET(loadcell_read_average(&d->cell, n, value,
                                                     timeout_ms));
}

bool hx711_last_saturated(hx711_handle_t d)
{
    return d ? loadcell_last_saturated(&d->cell) : false;
}

/* ------------------------------------------------------- calibration ----- */

loadcell_handle_ret hx711_tare(hx711_handle_t d, int n, uint32_t timeout_ms)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);
    return LOADCELL_HANDLE_RET(loadcell_tare(&d->cell, n, timeout_ms));
}

loadcell_handle_ret hx711_calibrate(hx711_handle_t d, float known_weight, int n,
                                    uint32_t timeout_ms)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);
    return LOADCELL_HANDLE_RET(loadcell_calibrate(&d->cell, known_weight, n,
                                                  timeout_ms));
}

void hx711_set_offset(hx711_handle_t d, int32_t offset)
{
    if (d)
        loadcell_set_offset(&d->cell, offset);
}

void hx711_set_scale(hx711_handle_t d, float counts_per_unit)
{
    if (d)
        loadcell_set_scale(&d->cell, counts_per_unit);
}

int32_t hx711_offset(hx711_handle_t d) { return d ? d->cell.offset : 0; }
float hx711_scale(hx711_handle_t d) { return d ? d->cell.scale : 0.0f; }

loadcell_handle_ret hx711_read_units(hx711_handle_t d, int n, float *units,
                                     uint32_t timeout_ms)
{
    if (!d)
        return LOADCELL_HANDLE_RET(-EINVAL);
    return LOADCELL_HANDLE_RET(loadcell_read_units(&d->cell, n, units,
                                                   timeout_ms));
}

struct loadcell_dev *hx711_loadcell(hx711_handle_t d)
{
    return d ? &d->cell : NULL;
}

#endif /* CONFIG_LOADCELL_HX711 */
