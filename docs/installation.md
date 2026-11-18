# Installation

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

The repository is `mcu-loadcell`; the forge package key, C API and
configuration options are `loadcell`. The clone command below names the
ESP-IDF component directory `loadcell`. It replaces two components,
`esp-hx711-loadcell` and `esp-nau7802-loadcell` (forge keys `hx711` and
`nau7802`): a project that `REQUIRES` either now `REQUIRES loadcell`.

Three step-by-step guides come first: [ESP-IDF](#esp-idf-step-by-step),
[STM32 with CMake](#with-cmake-a-cubemx-cmake-project) and
[STM32 without CMake](#without-cmake-stm32cubeide-a-makefile). None
needs a forge; if you build with esp-forge or stm-forge, see
[With forge](#with-forge). The steps use the NAU7802; for the HX711,
change the chip name. The reference follows the guides.

## ESP-IDF, step by step

**You need** ESP-IDF 5.2 or newer, with `idf.py` working in your terminal, and
an ESP-IDF project: a folder with a `CMakeLists.txt` and a `main/` folder.
`idf.py create-project my-project` makes one.

ESP-IDF calls a library a **component**: a folder with its own
`CMakeLists.txt` that the build finds and compiles. This library is the
component `loadcell`.

1. **Get the library.** Pick one way.

   - **Component manager**, the simplest: the first build downloads the
     library into `managed_components/loadcell`. Create
     `main/idf_component.yml`:

     ```yaml
     dependencies:
       loadcell:
         git: https://github.com/JonasAtelier/mcu-loadcell.git
         version: stable
     ```

   - **Clone**, if you want to edit the library or build offline. In the
     project folder:

     ```sh
     git clone https://github.com/JonasAtelier/mcu-loadcell components/loadcell
     ```

     Keep the folder name `loadcell`: ESP-IDF names a component after its
     folder.

   ```
   my-project/
   ├── CMakeLists.txt
   ├── sdkconfig.defaults       step 2
   ├── main/
   │   ├── CMakeLists.txt       step 3
   │   ├── idf_component.yml    step 1, component manager
   │   └── my-project.c         step 4
   └── components/
       └── loadcell/            step 1, clone
   ```

2. **Turn your chip on.** No chip driver is built until you do. Create
   `sdkconfig.defaults` in the project folder, a text file of settings you
   choose, with one line per chip:

   ```ini
   CONFIG_LOADCELL_NAU7802=y
   ```

   The other chip is `CONFIG_LOADCELL_HX711`. ESP-IDF copies these lines
   into `sdkconfig`, the project's full settings file, only when it creates
   that file: **if `sdkconfig` already exists, delete it** so your line is
   read. `idf.py menuconfig` → **Component config → Load cell ADC
   subsystem** does the same from a menu.

3. **Let your code see the library.** A new project's `main/CMakeLists.txt`
   has no `REQUIRES` and already sees every component. If yours has a
   `REQUIRES` list, add `loadcell` and `driver` to it:

   ```cmake
   idf_component_register(SRCS "main.c" REQUIRES loadcell driver)
   ```

4. **Write the smallest test.** Copy
   [examples/esp/nau7802/main/main.c](../examples/esp/nau7802/main/main.c)
   over your main source file and set `PIN_SDA`, `PIN_SCL` and
   `KNOWN_WEIGHT` to your board. [API usage](usage.md) explains the calls.

5. **Build.**

   ```sh
   idf.py set-target esp32    # your chip: esp32s3, esp32c3, ...
   idf.py build
   ```

   A good build ends with `Project build complete.` Flash it as any ESP-IDF
   app and open the serial monitor: the driver's first line ends in
   `loadcell: nau7802: NAU7802 up: channel 1, gain x128, 80 SPS, internal LDO`.

**If it fails**

| The build prints | Why | Fix |
|---|---|---|
| `Failed to resolve component 'loadcell' required by component 'main': unknown name.` | The clone is not in a folder named `loadcell` | Rename it to `components/loadcell` |
| `fatal error: nau7802.h: No such file or directory` | `main/CMakeLists.txt` has a `REQUIRES` list without `loadcell` | Step 3 |
| `loadcell: CONFIG_LOADCELL is on but no chip driver is selected` (a CMake warning) | No chip turned on | Step 2 |
| ``undefined reference to `nau7802_init'`` | That chip is not turned on, often because `sdkconfig` existed before you edited `sdkconfig.defaults` | Delete `sdkconfig`, build again |

## STM32, step by step

**You need** a project for an STM32 (F4, F7 or G0) that uses ST's **HAL**,
the vendor library behind `HAL_Init()`, `HAL_I2C_Init()` and friends, with
the I2C (NAU7802) or two GPIO pins (HX711) your chip is wired to already set
up. STM32CubeMX makes such a project; with **Toolchain / IDE: CMake** it
makes a CMake one, the main case below.

Two terms. A **HAL module** is the part of the HAL for one peripheral: I2C,
GPIO. It is compiled only when it is switched on in **hal_conf**, the file
`Core/Inc/stm32f4xx_hal_conf.h` (`stm32f7xx_hal_conf.h` on an F7,
`stm32g0xx_hal_conf.h` on a G0). CubeMX switches a module on when you enable
that peripheral.

### With CMake (a CubeMX CMake project)

1. **Get the library.** In the project folder, next to the top-level
   `CMakeLists.txt`:

   ```sh
   git clone https://github.com/JonasAtelier/mcu-loadcell
   ```

   ```
   my-board/
   ├── CMakeLists.txt                  step 2
   ├── cmake/stm32cubemx/              made by CubeMX
   ├── Core/
   │   ├── Inc/stm32f4xx_hal_conf.h    hal_conf, step 3
   │   └── Src/main.c                  step 4
   ├── Drivers/                        the HAL, copied in by CubeMX
   └── mcu-loadcell/                   step 1
   ```

2. **Add it to the build.** In the top `CMakeLists.txt`, after the line
   `add_subdirectory(cmake/stm32cubemx)`, add:

   ```cmake
   set(CONFIG_LOADCELL_NAU7802 ON)     # your chip; one line per chip
   set(MCU_HAL_TARGET stm32cubemx)
   set(MCU_DEVICE_HEADER stm32f4xx.h)  # stm32f7xx.h, stm32g0xx.h
   add_subdirectory(mcu-loadcell)
   ```

   and add `loadcell` to the link line CubeMX already wrote, so it reads:

   ```cmake
   target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx loadcell)
   ```

   Do not add `PRIVATE`: CubeMX's line has no such keyword, and CMake refuses
   to mix the two forms on one target.

   `stm32cubemx` is a CMake *target*, a named bundle of settings, that CubeMX
   makes to carry the HAL's include folders and defines; `MCU_HAL_TARGET`
   lends those to the library, which works with any CubeMX version.
   `MCU_DEVICE_HEADER` is your chip family's main header.

3. **Check the HAL module.** The NAU7802 talks I2C, so hal_conf must have
   `#define HAL_I2C_MODULE_ENABLED`; enabling I2C1 (or any I2C) in CubeMX
   does that. The [HAL module table](#stm32-hal) lists what every chip
   needs.

4. **Write the smallest test.** It is the `scale_task()` of
   [examples/stm/nau7802/main.c](../examples/stm/nau7802/main.c), without
   its calibration against a known weight, fitted into CubeMX's
   `Core/Src/main.c`. Paste each part between the `USER CODE BEGIN` and
   `END` comments named, so a regenerate keeps it.

   After `/* USER CODE BEGIN Includes */`:

   ```c
   #include "loadcell.h"
   #include "loadcell_log.h"
   #include "nau7802.h"
   ```

   After `/* USER CODE BEGIN 2 */`, which comes after `MX_I2C1_Init()`:

   ```c
     const nau7802_config_t cfg = {
       .gain = NAU7802_GAIN_128, /* a load cell puts out millivolts */
       .rate = NAU7802_RATE_80SPS,
       .ldo = NAU7802_LDO_3V3,
       .use_internal_ldo = true, /* excite the bridge from the chip */
     };
     nau7802_handle_t h;
     struct loadcell_dev *cell;

     /* -ENXIO: nothing at 0x2A. -EPROTO: the chip's calibration failed. */
     if (nau7802_init(&hi2c1, &cfg, &h))
       Error_Handler();
     cell = nau7802_loadcell(h);
     HAL_Delay(2000); /* keep the cell clear */
     if (loadcell_tare(cell, 20, 1000))
       Error_Handler();
   ```

   After `/* USER CODE BEGIN 3 */`, inside the `while (1)` loop:

   ```c
       struct loadcell_sample sample;

       if (loadcell_read(cell, &sample, 1000) == 0)
         loadcell_log_sample(cell, &sample);
   ```

   The lines go out through `printf()`: send that to a UART by writing
   newlib's `_write()`, and add this to `CMakeLists.txt` so numbers are
   printed:

   ```cmake
   target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
   ```

   [API usage](usage.md) explains the calls.

5. **Build** as before, for example `cmake --build` with your build folder.
   A good build shows a `Linking C executable` line and no error. On the
   UART, the driver's first line reads
   `loadcell: nau7802: NAU7802 up: channel 1, gain x128, 80 SPS, internal LDO`.

**If it fails**

| You see | Why | Fix |
|---|---|---|
| `loadcell: no chip driver is selected; set(CONFIG_LOADCELL_<CHIP> ON) before add_subdirectory()` (a CMake warning), then ``undefined reference to `nau7802_init'`` | No chip turned on, or turned on after `add_subdirectory(mcu-loadcell)` | Step 2 |
| `#error "the NAU7802 driver needs the HAL I2C module: ...` | The HAL module is off | Enable the peripheral in CubeMX and regenerate |
| `#error "loadcell: CMSIS_device_header is not defined; ...` | `MCU_DEVICE_HEADER` is missing | Step 2 |
| `fatal error: stm32f4xx.h: No such file or directory` | `MCU_HAL_TARGET` is missing | Step 2 |
| `Target "stm32cubmx" not found.` (a CMake error) | `MCU_HAL_TARGET` is misspelt | Step 2 |
| `The plain signature for target_link_libraries has already been used with the target` | `PRIVATE` added to the link line | Step 2 |
| No error, but the numbers print as blanks | The C library (newlib-nano) leaves out `%f` | The `-u _printf_float` line of step 4 |

### Without CMake (STM32CubeIDE, a Makefile)

1. Copy `include/` and `src/` of this repository into your project, for
   example to `Middlewares/mcu-loadcell/`, keeping the chip folders inside
   `src/`.
2. Add every `.c` file under `src/` to the build, chip folders included.
3. Add two include paths, `Middlewares/mcu-loadcell/include` and
   `Middlewares/mcu-loadcell/src`, and two defines:
   `CMSIS_device_header="stm32f4xx.h"` (or your family's), quotes included,
   and `CONFIG_LOADCELL_NAU7802=1`, one per chip. CubeMX already defines
   `USE_HAL_DRIVER` and your device, such as `STM32F405xx`.
4. Steps 3 to 5 of the CMake guide apply as they are; `-u _printf_float`
   goes in the linker flags.

Where these settings usually live: in STM32CubeIDE, **Project → Properties →
C/C++ Build → Settings → Tool Settings → MCU GCC Compiler** (Include paths,
Preprocessor); in a CubeMX Makefile, `C_SOURCES`, `C_INCLUDES` and `C_DEFS`.
The build errors are the ones in the table above; the fix is the matching
define or include path instead of a CMake line. One more:
`#include expects "FILENAME" or <FILENAME>` means `CMSIS_device_header` lost
its quotes.

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

Setup: [ESP-IDF, step by step](#esp-idf-step-by-step). For reference:

- In `main/idf_component.yml`, `version` is a git ref; `stable` is the
  released one.
- A clone in `components/` is a local component, named after its folder;
  that name is what other components `REQUIRES`. The examples in
  `examples/esp/` need no name: their `main` lists no `REQUIRES`, and ESP-IDF
  gives `main` every component, so they build from any checkout name.
- With the subsystem on and no chip picked, the build warns.
- ESP-IDF 5.2 or newer is required for the `i2c_master` API.

## STM32 HAL

Setup: [With CMake](#with-cmake-a-cubemx-cmake-project) or
[Without CMake](#without-cmake-stm32cubeide-a-makefile). The
`add_subdirectory()` route builds a static library `loadcell`. For
reference:

- Each `CONFIG_LOADCELL_<CHIP>` (and `CONFIG_LOADCELL_DEBUG`) that is true
  reaches the compiler as `CONFIG_LOADCELL_<CHIP>=1`, for the library and
  your app.
- `MCU_HAL_TARGET` is a target that carries your `USE_HAL_DRIVER`, device
  define (`STM32F405xx`, ...) and the include dirs of the HAL, CMSIS and your
  `stm32<family>xx_hal_conf.h`. The library copies only those include dirs
  and defines, never the target's sources, so this works with any CubeMX
  version. `MCU_DEVICE_HEADER` becomes `CMSIS_device_header`, which CubeMX
  does not define. Every `mcu-*` library reads these two, so set them once.
  Leave them unset if your project already provides both.
- The CPU flags (`-mcpu`, `-mfloat-abi`, ...) must reach the library too;
  CubeMX's toolchain file sets them for every target.
- Without CMake, `src/` only needs to be on the include path of this
  library's own sources; the defines are what stm-forge passes.

**No chip driver is built by default.** The core is always built and each
driver is opt-in: a chip whose option is absent or 0 compiles to nothing.
With the library on and no chip picked, the CMakeLists warns (ESP-IDF and
`add_subdirectory()`) and `src/loadcell_port_stm.c` emits a `#warning`.

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
to your app's `CMakeLists.txt` (in a CubeMX project the target is
`${CMAKE_PROJECT_NAME}`), or the unit lines print blanks where the numbers
go.
