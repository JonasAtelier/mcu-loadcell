# Architecture, driver development and tests

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

## Repository layout

```
include/                    public API, the only directory apps include
  loadcell.h                struct loadcell_dev, loadcell_read(), tare, calibrate, units
  loadcell_log.h            loadcell_err/warn/info/dbg, loadcell_log_sample()
  hx711.h nau7802.h         one header per chip: its handle API and <chip>_loadcell()
src/                        private to the library
  loadcell.c                the core: read, average, tare, calibrate, units
  loadcell_log.c            loadcell_log_sample()
  loadcell_math.h           24-bit sign extension, units, scale, saturation
  loadcell_priv.h           bus ops, attach and probe, pins, delay, mask, waits, errno maps
  loadcell_port_esp.c       all of that on ESP-IDF; empty elsewhere
  loadcell_port_stm.c       all of that on the STM32 HAL; empty under ESP-IDF
  hx711/                    hx711.c  hx711_proto.h  hx711_math.h  hx711_priv.h
  nau7802/                  nau7802.c  nau7802_regs.h  nau7802_math.h  nau7802_priv.h
test/                       host tests, `make -C test test`
  test_<chip>_math.c        the original drivers' maths tests
  core_cases.h              core cases, shared by esp/ and stm/
  hx711_cases.h             an HX711 on its two wires and its cases, shared
  nau7802_cases.h           a NAU7802 register file and its cases, shared
  esp/                      suites on idf_stubs/, through loadcell_port_esp.c,
                            the original drivers' own test_hx711.c and test_nau7802.c among them
  stm/                      suites on hal_stubs/, through loadcell_port_stm.c
  idf_stubs/  hal_stubs/    the slice of ESP-IDF and of the HAL the code calls
examples/
  esp/<chip>/               ESP-IDF projects: hx711, nau7802
  stm/<chip>/               STM32 snippets: hx711, nau7802
docs/                       the five guides; datasheet PDFs here are gitignored
assets/                     README and wiring figures
CMakeLists.txt              ESP-IDF only; stm-forge globs src/**/*.c instead
Kconfig                     LOADCELL and LOADCELL_<CHIP>, for ESP-IDF and both forges' menus
idf_component.yml           ESP-IDF component manifest
```

Naming inside `src/<chip>/`, the same in every category library:

| File | Holds |
|---|---|
| `<chip>.c` | the driver: init, del, the ops, the chip's calls |
| `<chip>_regs.h` | register map: addresses, bit fields, values, each with its datasheet citation |
| `<chip>_proto.h` | serial protocol: commands, frames, pulse counts, checksums, for parts without registers |
| `<chip>_math.h` | pure conversions, compiled and asserted on the host |
| `<chip>_priv.h` | driver state, only if needed |

The chips' `_math.h` keep the originals' function names as aliases of
`loadcell_math.h`, so the originals' maths tests run unchanged. The chip
drivers keep the originals' four-space style; the library's own files use
tabs.

## Handles first, calibration once

As in mcu-stepper: the chips' calls already had public names taking a
handle, and the ESP-IDF headers had to stay as they were, so the handle
stays the device. `struct hx711_t` and `struct nau7802_t` each embed a
`struct loadcell_dev`, init fills it and points `priv` back at the handle,
and `hx711_loadcell()` / `nau7802_loadcell()` hand it out.

Unlike mcu-stepper, the two originals shared more than a shape: averaging,
tare, calibration against a known mass and conversion to units were the
same code, line for line. That now lives once in `src/loadcell.c`, on the
`offset`, `scale` and `saturated` the dev holds; each chip's own calls are
wrappers, and each chip supplies only `is_ready` and `read_raw`, setting
`saturated` from the reading.

## Platform ports

The drivers in `src/` are the same C on both platforms; what differs is in
`src/loadcell_port_esp.c` and `src/loadcell_port_stm.c`:

| Helper | ESP-IDF | STM32 HAL |
|---|---|---|
| bus ops, attach, `loadcell_i2c_probe()` | `i2c_master`, `spi_master`, UART driver | HAL I2C, SPI, UART |
| `loadcell_pin_config/set/get()` | `gpio_config()`, `gpio_set/get_level()` | nothing; `HAL_GPIO_WritePin/ReadPin()` |
| `loadcell_delay_us()` | `esp_rom_delay_us()` | weak, on SysTick ([chips.md](chips.md#timing-on-the-hal)) |
| `loadcell_irq_off/restore()` | `portENTER/EXIT_CRITICAL()` on one spinlock | PRIMASK saved, `__disable_irq()`, restored |
| `loadcell_tick_now/expired/poll_wait()` | FreeRTOS ticks, whole ticks + 1, at least one tick a poll | `HAL_GetTick()`, strictly more than the limit, `HAL_Delay()` |
| `loadcell_time_us()` | `esp_timer_get_time()` | weak, `HAL_GetTick()` × 1000 |

The handle calls return `esp_err_t` on ESP-IDF and errno on the HAL: the
drivers work in errno and `LOADCELL_HANDLE_RET()` converts once where each
public call returns (`loadcell_esp_err()`).

Each chip file includes `loadcell_priv.h` before its `#if
CONFIG_LOADCELL_<CHIP>`: on ESP-IDF that is what brings in `sdkconfig.h`,
and a chip file that tested the option first would compile to nothing.

## Bus abstraction

The bus layer is mcu-stepper's: `struct loadcell_bus_ops` with
`transfer()`, `exchange()` (SPI frames, UART echoes), `receive()` (UART)
and `detach()`, picked at run time by which attach the driver calls. Every
category library offers I2C, SPI and UART, used or not; the NAU7802 is on
I2C, and `test/{esp,stm}/test_port_*` check all three. The I2C presence
probe is mcu-encoder's: `-ENXIO` when nothing ACKs, decided before anything
is allocated, because a NACK in the middle of a transfer is a plain `-EIO`.

The HX711 is not a bus chip and sits outside the bus layer, on the pin,
delay and mask helpers.

## Adding a chip

1. `include/foo.h`: the chip's handle API, with the platform's handle types
   behind `#ifdef ESP_PLATFORM`, and `struct loadcell_dev *foo_loadcell(foo_handle_t)`.
2. `src/foo/foo.c`, in a folder of its own. Include `loadcell_priv.h`
   first, then wrap the driver in `#if CONFIG_LOADCELL_FOO` (never
   `#ifdef`). Embed a `struct loadcell_dev` in the handle; give it
   `is_ready` and `read_raw` ops, the latter setting `saturated`. Write the
   chip's tare/calibrate/units calls as wrappers over the core.
3. In init: set `cell.name` first, refuse a bad config, check presence,
   allocate, attach, configure, and only on success set `cell.ops` and
   `cell.priv`. Work in errno; return `LOADCELL_HANDLE_RET(err)`.
4. Add `CONFIG_LOADCELL_FOO` to the no-chip checks in `CMakeLists.txt` and
   `src/loadcell_port_stm.c`, a HAL module `#error` to `src/loadcell_priv.h`
   if it needs one, a `src/foo/foo.c` line to `CMakeLists.txt`, a `config
   LOADCELL_FOO` to `Kconfig`, `# LOADCELL_FOO is not set` to both forges'
   defconfigs, and its tests to `test/Makefile`.

## Tests

```sh
make -C test test
```

Host-only; no toolchain, HAL or ESP-IDF needed. Nothing is checked against
real silicon.

- **The original drivers' own tests** run with plumbing changes only:
  `test/esp/test_nau7802.c` is its `test_driver.c` unchanged;
  `test/esp/test_hx711.c` is its `test_driver.c` with one include renamed
  (`hx711_regs.h` → `hx711_proto.h`); `test/test_hx711_math.c` and
  `test/test_nau7802_math.c` are their `test_math.c` with the include paths
  changed (`../src/<chip>/`). Their stubs are merged into `test/idf_stubs/`:
  the FreeRTOS clock there is weak and moves a tick each time it is read,
  so a deadline still ends under their do-nothing `vTaskDelay()`; and
  `i2c_master_probe()` defaults to "something answers", as their chip
  always did. The merged `esp_err.h` has the real `ESP_ERR_INVALID_CRC`,
  0x109 (the NAU7802 original's stub said 0x10B).
- **This library's own cases** are written once, in `test/*_cases.h`,
  against fakes that know nothing of the drivers, and both platforms run
  them from their own calls.
- **The deadline regression:** `test_deadline_is_time` in both chips' cases
  puts a conversion 900 ms away at a 100 Hz tick and asks for it with a
  1000 ms timeout. With the originals' wait loop pasted back in, both fail
  it; their own tests pass either way, which is how the bug lived.

| Binary | Checks |
|---|---|
| `test_hx711_math`, `test_nau7802_math` | the originals': sign extension, gain and rate tables, LDO, units, scale solving and its 100-count floor, saturation, register addresses |
| `esp/test_hx711` | the original's: config refusals, an absent chip timing out, reads and sign, no power-down during a read, gain pulses on the wire, calibration, saturation, power, NULLs |
| `esp/test_nau7802` | the original's: the start-up order, config refusals, CAL_ERR reported, reads, a timeout, gain and channel re-running the trim, calibration, saturation, NULLs |
| `esp/test_core`, `stm/test_core` | `loadcell_read()` stamping after the conversion, units and saturation in the sample, a failed read leaving it alone; averaging in 64 bits; tare, its refusal when saturated with the offset kept; calibration and its refusals keeping the scale; stored values, zero scale ignored; unbound devices refused without a call; log lines, 64-bit timestamps without `%lld`; on the HAL the default clock and `loadcell_dbg()` |
| `esp/test_hx711_shared`, `stm/test_hx711` | a fake HX711 on its wires, timing every high phase on the platform's own clock: init's power cycle, the category view, one offset and scale through both APIs, saturation and the refused tare; 25 pulses a read, every high phase masked and none a power-down, the mask put back; 25/26/27 pulses for the three gains; **a conversion 900 ms away waited for at a 1000 ms timeout, and a 1100 ms one timed out only after 1000 ms**; an absent chip timing out after its full second, nothing leaked; config refusals; power down and up. ESP-IDF: pins configured input and output, GPIO numbers refused. HAL: ports refused when NULL or the same pin, PRIMASK as found |
| `esp/test_nau7802_shared`, `stm/test_nau7802` | a fake NAU7802 register file: nothing at 0x2A refused before anything is allocated; start-up and the category view; PUR never coming given 200 ms, CAL_ERR as `-EPROTO`, a calibration never ending given 400 ms, a bus failure, nothing leaked; CALMOD written `00` with the rest of CTRL2 kept; reads, calibration and saturation through both APIs; **the same 900/1100 ms deadline checks**; raw registers, standby, gain and channel re-running the trim. ESP-IDF: the device at 0x2A and `scl_hz`, removed after a failed init, NOT_FOUND for an absent chip. HAL: a NULL handle refused, the address shifted |
| `esp/test_port_esp`, `stm/test_port_stm` | the bus ops as in mcu-stepper (I2C, SPI, UART with exchange and receive, the HAL's no-overrun echo), plus the presence probe and its codes; the pins; ESP-IDF: `esp_rom_delay_us()` and the spinlock entered and left in pairs, and a 1000 ms deadline at 100 Hz taking 101 ticks of clock however many polls; HAL: the SysTick delay never early for 1–64 µs, from every phase across a reload, with coarse reads, on the core clock / 8 -- exactly at the boundary from every phase within a count, every counter value and read spacings of 1–8 cycles -- on a slower core, and the busy loop with SysTick stopped; PRIMASK put back masked or not; the waits |

All build under `-std=gnu11 -Wall -Wextra -Werror -pedantic` with UBSan
(`-fsanitize=undefined -fno-sanitize-recover=all`), and `-Wl,--wrap` (GNU
ld) to count allocations and capture `printf()`. The originals' tests built
under `-std=c11`; the `loadcell_*` log macros need gnu11's
`, ##__VA_ARGS__`.
