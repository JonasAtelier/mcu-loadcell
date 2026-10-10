<p align="center">
  <img src="assets/loadcell.svg" width="100%" alt="A load cell bridge under a weight, read out by an HX711">
</p>

# mcu-loadcell

<p align="center">
  <img alt="platform: ESP-IDF | STM32 HAL" src="https://img.shields.io/badge/platform-ESP--IDF%20%7C%20STM32%20HAL-70e1b2?style=flat-square&labelColor=0d2238">
  <img alt="language: C" src="https://img.shields.io/badge/language-C-70e1b2?style=flat-square&labelColor=0d2238">
  <img alt="license: MIT" src="https://img.shields.io/badge/license-MIT-f2bd68?style=flat-square&labelColor=0d2238">
</p>

A small C load cell ADC library for **ESP-IDF 5.2+** and **STM32 HAL**. Two
24-bit bridge converters keep their own handle APIs and share one
`struct loadcell_dev`: timestamped samples, averaging, tare, calibration
against a known mass, and conversion to your unit. This library replaces
`esp-hx711-loadcell` and `esp-nau7802-loadcell`; the STM32 port is new.

The repository is named `mcu-loadcell`; the C API, `CONFIG_LOADCELL_*`
options and forge package key are `loadcell`. The clone command below names
the ESP-IDF component directory `loadcell` as well.

> **Hardware unverified.** Host tests use simulated devices and platform stubs.
> Nothing here has run on a physical chip.

[Supported hardware](#supported-hardware) · [Install](#install) ·
[Quick start](#quick-start) · [Examples](#examples) ·
[Documentation](#documentation) · [Tests](#tests)

## Supported hardware

Both drivers support both platforms. Enable only the drivers you need.

| Chip | Interface | Notes |
|---|---|---|
| **HX711** | two GPIO pins, bit-banged | gain 128/64 on channel A, 32 on B; 10 or 80 SPS by its RATE pin; interrupts masked for each clock pulse's microsecond high phase |
| **NAU7802** | I2C, 0x2A | PGA x1–x128, 10–320 SPS, two channels, internal LDO to excite the bridge, hardware offset trim |

Wiring for both is in the [wiring guide](docs/hardware.md).

## Install

### ESP-IDF

Clone into your project's components directory:

```sh
git clone https://github.com/JonasAtelier/mcu-loadcell components/loadcell
```

Enable your chips in `idf.py menuconfig` → **Component config → Load cell
ADC subsystem**, or add this to `sdkconfig.defaults` for the HX711:

```ini
CONFIG_LOADCELL=y
CONFIG_LOADCELL_HX711=y
```

The `main` component sees the library automatically. Other components declare
`REQUIRES loadcell driver` in `idf_component_register()`.

### esp-forge / stm-forge

stm-forge must be at `ee921c3` or later, which builds the chip folders in
`src/`; older ones fail at link with undefined chip functions.

Add to `profiles/pkgversion`:

```make
loadcell_GIT=https://github.com/JonasAtelier/mcu-loadcell@stable
```

Enable the package and your chip in `configs/<target>_defconfig`:

```make
PKG_loadcell        = y
LOADCELL_HX711      = y
```

### STM32 HAL without forge

Compile every `.c` under `src/`, chip folders included, add `include/` and
`src/` to the include path, and define `CMSIS_device_header` as your family
header, `USE_HAL_DRIVER`, and `CONFIG_LOADCELL_<CHIP>=1` for each selected
chip. The NAU7802 needs the HAL I2C module; the HX711 only GPIO.

**No chip driver is enabled by default.** See the
[installation guide](docs/installation.md) for all options.

## Quick start

On the STM32 HAL, with DOUT an input and PD_SCK a push-pull output the app
has set up:

```c
#include "hx711.h"
#include "loadcell.h"
#include "loadcell_log.h"

int weigh_once(GPIO_TypeDef *port, uint16_t dout, uint16_t sck)
{
    const hx711_config_t cfg = {
        .dout_port = port, .dout_pin = dout,
        .sck_port = port, .sck_pin = sck,
        .gain = HX711_GAIN_A128,
    };
    struct loadcell_sample sample;
    hx711_handle_t h;
    int ret = hx711_init(&cfg, &h);
    if (ret)
        return ret;

    loadcell_set_offset(hx711_loadcell(h), 84211);  /* from a tare */
    loadcell_set_scale(hx711_loadcell(h), 412.6f);  /* from a calibration */
    ret = loadcell_read(hx711_loadcell(h), &sample, 1000);
    if (!ret)
        loadcell_log_sample(hx711_loadcell(h), &sample);

    hx711_del(h);
    return ret;
}
```

On ESP-IDF the config takes GPIO numbers instead, and the driver sets the
pins up itself; the [examples](examples/esp/) walk through tare and
calibration.

- `loadcell_read()` gives a sample: `counts`, `units` from the offset and
  scale, `saturated`, and `t_us`. `loadcell_tare()` and
  `loadcell_calibrate()` measure the two numbers; store them and set them at
  boot.
- On ESP-IDF the chip calls return `esp_err_t`, as the original drivers did;
  on the HAL, **0 or a negative errno**. `loadcell_*` is errno on both.
- Buses are the app's; the HX711's two pins are the driver's on ESP-IDF and
  the app's on the HAL. See [API usage](docs/usage.md).

## Examples

| Chip | ESP-IDF | STM32 HAL |
|---|---|---|
| HX711 | [hx711](examples/esp/hx711/) | [hx711](examples/stm/hx711/main.c) |
| NAU7802 | [nau7802](examples/esp/nau7802/) | [nau7802](examples/stm/nau7802/main.c) |

ESP-IDF examples locate this library through `EXTRA_COMPONENT_DIRS`, which
assumes the repository is checked out as `loadcell`. With ESP-IDF activated:

```sh
cd examples/esp/hx711
idf.py set-target esp32 # Replace with your board's target.
idf.py build
```

STM32 examples are snippets to integrate into your application.

## Scope and limitations

- **No filtering beyond averaging**, no temperature compensation, no
  persistence: the offset and scale live in RAM until you store them.
- **Blocking reads.** Each read waits for a conversion, at most its timeout;
  the wait polls, sleeping at least a tick between looks.
- **HX711 on the HAL:** microsecond timing from SysTick, which `HAL_Init()`
  runs; an app on another timebase defines its own `loadcell_delay_us()`.
- **Concurrency:** no locks. Use one thread per device and bus, or supply a
  mutex.

## Documentation

| Guide | Contents |
|---|---|
| [Installation](docs/installation.md) | Forge, ESP-IDF, STM32 HAL, build options and logging setup |
| [Hardware](docs/hardware.md) | Wiring and pins for each chip |
| [API usage](docs/usage.md) | Handles and the category view, calibration, timing, errors, changes from the originals |
| [Chip reference](docs/chips.md) | Pulses and gain, masking and timing, registers and start-up, provenance |
| [Development](docs/development.md) | Platform ports, bus abstraction, adding a driver and test coverage |

## Tests

```sh
make -C test test
```

Host-only tests use stubbed HAL and ESP-IDF APIs; no embedded toolchain or
physical chip is needed. See the [test reference](docs/development.md#tests).
Error logs during the suite are expected: tests deliberately exercise
failure paths.

## License

[MIT](LICENSE)
