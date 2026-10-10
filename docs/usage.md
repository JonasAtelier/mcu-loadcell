# API usage

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

## Handles and the category view

Each chip keeps the handle API its original driver had: `hx711_init()` /
`nau7802_init()` fill a handle, every chip call takes it, and `hx711_del()`
/ `nau7802_del()` free it. The handle holds a `struct loadcell_dev`, and
`hx711_loadcell(h)` / `nau7802_loadcell(h)` hand it out:

| Call | Does |
|---|---|
| `loadcell_read(c, &s, timeout_ms)` | one conversion as a sample: `counts`, `units`, `saturated`, `t_us` |
| `loadcell_is_ready(c, &ready)` | whether a conversion is waiting |
| `loadcell_read_average(c, n, &counts, timeout_ms)` | the mean of n conversions |
| `loadcell_tare(c, n, timeout_ms)` | nothing on the cell: the mean becomes the offset |
| `loadcell_calibrate(c, weight, n, timeout_ms)` | a known mass on it: the scale, counts per unit |
| `loadcell_read_units(c, n, &units, timeout_ms)` | the mean in your unit |
| `loadcell_set_offset/_set_scale`, `loadcell_offset/_scale` | store and restore the calibration |
| `loadcell_last_saturated(c)` | the last conversion sat at an end of the range |
| `loadcell_log_sample(c, &s)` | one info line |

The offset and scale live in the `loadcell_dev`, so they are the same
numbers through either API: `hx711_tare()` and `loadcell_tare()` set one
offset, `hx711_scale()` reads what `loadcell_calibrate()` set. The
`struct loadcell_dev *` is valid from a successful init to the del; it is
never probed or removed on its own. `t_us` is stamped when the counts were
fetched; the conversion finished up to one poll earlier.

The chips' own extras stay theirs: gain and channel, power-down and up on
the HX711; the internal offset trim, gain, rate, channel, standby and raw
registers on the NAU7802. Each header documents them.

## Calibration is not optional

The chip gives counts. Counts become weight through two numbers you have to
measure, and neither can be derived from the part:

1. **Tare** with nothing on the cell → the offset.
2. **Calibrate** with a known mass on it → the scale, in counts per unit.

Store them and set them at startup, or the user re-tares every boot.
A tare against a saturated reading is refused (`-ERANGE`): an overloaded or
miswired cell has no zero. A calibration that moved under 100 counts, or a
weight that is not positive, is refused (`-EINVAL`) and the old scale kept.
On the NAU7802, `nau7802_calibrate_internal()` is something else again: the
chip trimming its own input offset, which init runs and every gain or
channel change re-runs.

## Buses and pins

- **NAU7802 on ESP-IDF:** an `i2c_master_bus_handle_t` the app has created;
  the driver checks something answers at 0x2A (`i2c_master_probe()`), then
  adds its device at `scl_hz` (0: 400 kHz) and removes it at del.
- **NAU7802 on the HAL:** an `I2C_HandleTypeDef *` the app has
  `HAL_I2C_Init()`ed; presence by `HAL_I2C_IsDeviceReady()`; the clock is the
  handle's and `scl_hz` is ignored.
- **HX711 on ESP-IDF:** two GPIO numbers; the driver configures DOUT as an
  input and PD_SCK as an output with `gpio_config()`, as the original did.
  They are two pins owned per chip, not a shared bus.
- **HX711 on the HAL:** two port/pin pairs the app has set up — DOUT an
  input, PD_SCK a push-pull output, low. The driver only reads and drives
  them.

## Timing and threads

**Reads block** until a conversion is ready, at most `timeout_ms`. The wait
polls every 2 ms, and the timeout is counted in clock time: on ESP-IDF in
FreeRTOS ticks, rounded up to whole ticks plus one, at least one tick per
poll; on the HAL in `HAL_GetTick()` milliseconds, strictly more than
`timeout_ms`. A timeout of 0 is one look; `UINT32_MAX` waits forever. At
10 SPS a conversion is 100 ms away, and after an HX711 power-up the first
one takes 400 ms (Table 2, p.3), so allow for it.

**HX711 interrupts.** Every clock pulse masks interrupts for its high phase,
about 2 µs: a critical section on one spinlock on ESP-IDF, PRIMASK on the
HAL, put back as it was. Between pulses interrupts run. A whole reading is
25–27 pulses, roughly 60 µs. Read from thread context.

**HX711 microseconds on the HAL** come from SysTick, through a weak
`loadcell_delay_us()`; see [the chip reference](chips.md#timing-on-the-hal).

**No locks.** Neither driver has one, as neither original did: one thread
per device, or your own mutex. The NAU7802 shares its bus with whatever else
is on it; that bus's own driver serialises transfers.

## Logging

On ESP-IDF everything goes under the log tag `loadcell`, prefixed with the
chip name, and `loadcell_dbg()` is `ESP_LOGD`. On the HAL every line goes
through `printf()` as `loadcell: <chip>: ...`. Your own code can use the same
`loadcell_err()`, `loadcell_warn()`, `loadcell_info()` and `loadcell_dbg()`
from `loadcell_log.h`. `loadcell_dbg()` is compiled out unless
`CONFIG_LOADCELL_DEBUG` is 1 (stm-forge: `LOADCELL_DEBUG = y`).

## Errors

`loadcell_*` calls return 0 or a negative errno on both platforms. The chip
calls return `esp_err_t` on ESP-IDF, as the original drivers did, and errno
on the HAL:

| Meaning | HAL | ESP-IDF |
|---|---|---|
| bad argument or config, NULL handle | `-EINVAL` | `ESP_ERR_INVALID_ARG` |
| a calibration mass that barely moved the reading | `-EINVAL` | `ESP_ERR_INVALID_ARG` |
| no memory for the handle | `-ENOMEM` | `ESP_ERR_NO_MEM` |
| no conversion in time; HX711 DOUT never low | `-ETIMEDOUT` | `ESP_ERR_TIMEOUT` |
| tare against a saturated reading | `-ERANGE` | `ESP_ERR_INVALID_STATE` |
| NAU7802: nothing answers at 0x2A | `-ENXIO` | `ESP_ERR_NOT_FOUND` |
| NAU7802: answers, never reports power-up ready | `-ENODEV` | `ESP_ERR_NOT_FOUND` |
| NAU7802: the chip's calibration reports CAL_ERR | `-EPROTO` | `ESP_ERR_INVALID_STATE` |
| bus busy (HAL_BUSY) | `-EAGAIN` | — |
| other bus failure, a NACK mid-transfer included | `-EIO` | `ESP_FAIL` |

On ESP-IDF a bus error passes through errno on its way up, and the map is
lossy in that direction: an IDF code with no errno of its own comes back as
`ESP_FAIL`. `strerror(-ret)` names any errno.

## Changes from esp-hx711-loadcell and esp-nau7802-loadcell

Both ESP-IDF headers are token-for-token the originals', plus
`hx711_loadcell()` / `nau7802_loadcell()` and the `loadcell.h` they include.
Behaviour changes, deliberately:

- **Waits are counted in time, not polls** (both chips). The originals
  polled with `vTaskDelay(pdMS_TO_TICKS(2))` and counted 2 ms per poll. At
  ESP-IDF's default 100 Hz tick `pdMS_TO_TICKS(2)` is 0, `vTaskDelay(0)` only
  yields, and a 1000 ms timeout was spent after 500 polls in microseconds.
  So at 100 Hz an HX711 init never waited out the 400 ms settling after its
  power cycle, a read at 10 SPS timed out before the conversion came, and
  the NAU7802's power-up and calibration loops gave up early. Now every poll
  sleeps at least one tick and the timeout is a tick deadline, as in
  esp-tmc5240-stepper; the NAU7802's power-up gets 200 ms and its
  calibration 400 ms, the originals' 100 and 200 tries of 2 ms. At a 1000 Hz
  tick the old loops happened to work, and the new ones wait the same.
- **The NAU7802 checks something answers at 0x2A first**, with
  `i2c_master_probe()`, before anything is allocated or written. Nothing
  there is now `ESP_ERR_NOT_FOUND`, as the header always said; the original
  got whatever the first transfer's NACK returned.
- Log lines go under the tag `loadcell` as `hx711: ...` / `nau7802: ...`,
  not under `hx711` / `nau7802`.
- Errors pass through errno inside the drivers, so an I2C error with no
  errno of its own comes back as `ESP_FAIL` rather than the original IDF
  code. The drivers' own codes are unchanged.

Tare, calibrate, average and units are now written once in the core; their
logic and their codes are the originals', and both originals' own tests run
with plumbing changes only (an include path or name).
