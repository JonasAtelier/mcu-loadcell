# Installation

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

The repository is `mcu-loadcell`; the forge package key, C API and
configuration options are `loadcell`. The clone command below names the
ESP-IDF component directory `loadcell`. It replaces two components,
`esp-hx711-loadcell` and `esp-nau7802-loadcell` (forge keys `hx711` and
`nau7802`): a project that `REQUIRES` either now `REQUIRES loadcell`.

## With forge

stm-forge must be at `ee921c3` or later ("build: libraries may keep sources
in src/ subfolders"); older ones only compile `src/*.c`, so the chip drivers
in `src/<chip>/` are never built and the link fails with undefined
references to the chip functions. esp-forge is unaffected: the component's
CMakeLists lists its sources.

With [esp-forge](https://github.com/JonasAtelier/esp-forge) or
[stm-forge](https://github.com/JonasAtelier/stm-forge). The catalog line in
`profiles/pkgversion` is the same for both:

```make
loadcell_GIT=https://github.com/JonasAtelier/mcu-loadcell@stable
```

and a target turns the library and its chips on in `configs/<target>_defconfig`:

```make
PKG_loadcell        = y
LOADCELL_HX711      = y
# LOADCELL_NAU7802 is not set
# LOADCELL_DEBUG is not set
```

or with `make menu`. esp-forge turns each line into `CONFIG_LOADCELL_<CHIP>=y`
in `sdkconfig.defaults`; stm-forge passes it as `-DCONFIG_LOADCELL_<CHIP>=1`.
`LOADCELL_DEBUG = y` turns on `loadcell_dbg()` lines on the HAL; on ESP-IDF
the log level decides.

## ESP-IDF

Clone into your project's `components/` directory. A local component is
named after its directory, and that name is what you `REQUIRES`:

```sh
git clone https://github.com/JonasAtelier/mcu-loadcell components/loadcell
```

```cmake
idf_component_register(SRCS "main.c" REQUIRES loadcell driver)
```

Enable the chips under `idf.py menuconfig` → **Component config → Load cell
ADC subsystem**, or put `CONFIG_LOADCELL_<CHIP>=y` in your
`sdkconfig.defaults` as each example in `examples/esp/` does. With the
subsystem on and no chip picked, the build warns. ESP-IDF 5.2 or newer is
required for the `i2c_master` API.

## STM32 HAL

Copy `include/` and `src/` into an STM32 project, compile every `.c` under
`src/`, chip folders included, add `include/` and `src/` to the include path
(`src/` only needs to reach this library's own sources), and define what the
forge would: `CMSIS_device_header` as your family header (`"stm32f4xx.h"`,
`"stm32f7xx.h"`, `"stm32g0xx.h"`), `USE_HAL_DRIVER`, and
`CONFIG_LOADCELL_<CHIP>=1` for each chip you want.

**No chip driver is built by default.** The core is always built and each
driver is opt-in: a chip whose option is absent or 0 compiles to nothing.
With the library on and no chip picked, the ESP-IDF CMakeLists warns and
`src/loadcell_port_stm.c` emits a `#warning`.

**The HAL modules are yours to enable:**

| Option | Needs in hal_conf | HAL source |
|---|---|---|
| `LOADCELL_NAU7802` | `HAL_I2C_MODULE_ENABLED` | `stm32<family>xx_hal_i2c.c` |
| `LOADCELL_HX711` | GPIO only, always on | `stm32<family>xx_hal_gpio.c` |

A chip whose module is off stops the build with an `#error` in
`src/loadcell_priv.h` that says which define is missing. The bus layer's SPI
and UART code sit under their own module switches and build to nothing
without them.

**The HX711's microseconds come from SysTick.** `HAL_Init()` starts it for
the HAL's 1 ms tick, and the library's weak `loadcell_delay_us()` counts it.
If your app runs SysTick off, or drives the HAL timebase from a TIM, define
your own `void loadcell_delay_us(uint32_t us)` that never returns early
(DWT->CYCCNT on an F4 or F7, or a free-running timer); see
[the chip reference](chips.md#timing-on-the-hal).

**printf() is the log.** Lines go out through `printf()`, so retarget
newlib's `_write()` to a UART. stm-forge links newlib-nano
(`--specs=nano.specs`), whose `printf()` drops `%f` unless the app links with
`-u _printf_float`: add `target_link_options(firmware PRIVATE -u _printf_float)`
to your app's `CMakeLists.txt`, or the unit lines print blanks where the
numbers go.
