# Chip reference

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

## HX711

The chip behind every cheap scale. Two wires, no bus: the part drives a data
line and you drive a clock, and the number of clock pulses decides both what
you read and what it measures next. Datasheet: AVIA Semiconductor HX711
(English), `docs/hx711.pdf` (gitignored).

| Function | Does |
|---|---|
| `hx711_init(&cfg, &h)` | Set the pins up (ESP-IDF), power cycle the part, one throwaway reading so the gain is in force |
| `hx711_is_ready` / `hx711_read_raw` / `hx711_read_average` | DOUT low; one conversion, sign extended from 24 bits; the mean of n |
| `hx711_tare` / `hx711_calibrate` / `hx711_read_units` | the core's calibration, on this chip |
| `hx711_set_offset/_set_scale`, `hx711_offset/_scale`, `hx711_last_saturated` | the stored numbers and the last reading's state |
| `hx711_set_gain(h, gain, timeout)` | gain and input, with a throwaway reading |
| `hx711_power_down` / `hx711_power_up` | under 1 µA down; up restores the gain |
| `hx711_del(h)` | power down, free |

### Pulses, gain and the one-reading delay

When DOUT goes low a conversion is ready. Each PD_SCK pulse shifts out one
bit, MSB first, 24 in all, two's complement; then 1, 2 or 3 more pulses pick
the input and gain of the **next** conversion — 25, 26 or 27 in all
("Serial Interface" and Table 3, p.4):

| Gain | Pulses | Input |
|---|---|---|
| 128 | 25 | Channel A |
| 32 | 26 | Channel B |
| 64 | 27 | Channel A |

Fig. 2 on p.5 labels the 27-pulse case "CH.B, Gain:64". Table 3 says
channel A, and the text on p.4 says channel B has a fixed gain of 32, so the
figure's label is taken as a slip and Table 3 is followed — as
esp-hx711-loadcell and the Linux driver do. The order is not by gain: 64
takes three pulses, 32 two. A gain change takes effect a reading later, so
`hx711_set_gain()` clocks a throwaway conversion through; so does power-up,
since a reset leaves channel A at gain 128 (p.5). Out-of-range input
saturates at 800000h or 7FFFFFh (p.4); a reading within one count of either
end is reported saturated.

### Masking, and the 60 µs that powers it down

PD_SCK held high for more than 60 µs powers the chip down (Fig. 3, p.5).
That is how power-down is requested — the driver holds it 64 µs — and it is
also what an interrupt or a task switch does by accident if it lands
between raising and lowering the clock: the result is a reset part and 24
bits that look like a measurement. The timing table under Fig. 2 (p.5)
gives the high time T3 as 0.2 µs minimum, 1 µs typical, 50 µs maximum, and
the low time T4 as 0.2 µs minimum; the driver uses 1 µs each.

So every pulse's high phase runs with interrupts masked: a critical section
on one spinlock on ESP-IDF, as the original did; PRIMASK saved, set and
restored on the HAL. The mask covers one pulse, not the transfer:
interrupts run between pulses, while the clock is low, which is harmless.
About 2 µs masked at a time instead of 60.

After a power-up the driver pauses 10 µs before it looks at DOUT. **That 10
is the driver's own choice, not a datasheet value:** the datasheet gives no
wake time, only that the part resets when PD_SCK returns low (p.5); the
wait for DOUT low that follows is what actually gates. The first conversion
after a reset takes the output settling time, 400 ms at 10 SPS and 50 ms at
80 SPS (Table 2, p.3).

### Timing on the HAL

The HAL has no microsecond delay. The library's weak `loadcell_delay_us()`
counts SysTick:

- Every Cortex-M has SysTick; the M0+ of an STM32G0 has no DWT cycle
  counter, so DWT would not do.
- `HAL_Init()` already runs it, from the core clock (or the core clock / 8
  when `CTRL.CLKSOURCE` is 0) for its 1 ms tick, and it keeps counting with
  interrupts masked, where the HX711 calls the delay.
- It sums VAL's down-count across reloads, and asks for
  `us × SystemCoreClock / 1 MHz` counts (÷ 8 for the slow clock), rounded
  up, plus one: two reads of a counter can sit up to a count apart from the
  time between them, depending on where each falls within a count. Time
  before its first read is not counted, and neither is a reload missed
  while an interrupt ran longer than a period: both can only make it
  longer. `test/stm/test_port_stm.c` checks it never returns early, from
  every phase of the counter across a reload, and at the slow clock from
  every phase within a count.
- With SysTick stopped it falls back to a busy loop of one iteration per
  core cycle: at least a cycle each, so never short either.

An app that drives the HAL timebase from a TIM and stops SysTick, or wants a
different counter, defines its own `void loadcell_delay_us(uint32_t us)`.
It must never return early.

## NAU7802

The same job as the HX711 over I2C: no microsecond timing, no masking.
It adds a PGA to x128, two independent channels, an internal LDO to excite
the bridge and a hardware offset trim. Datasheet: Nuvoton NAU7802 revision
1.7, `docs/nau7802.pdf` (gitignored).

| Function | Does |
|---|---|
| `nau7802_init(bus, &cfg, &h)` | Presence at 0x2A, then the 9.1 start-up, configuration, internal calibration and conversions started |
| `nau7802_is_ready` / `nau7802_read_raw` / `nau7802_read_average` | CR; one conversion from ADCO; the mean of n |
| `nau7802_tare` / `nau7802_calibrate` / `nau7802_read_units` | the core's calibration, on this chip |
| `nau7802_calibrate_internal(h)` | the chip's own offset trim (CALS, CAL_ERR) |
| `nau7802_set_gain` / `_set_rate` / `_set_channel` | gain and channel re-run the internal trim |
| `nau7802_sleep(h, on)` | standby: PUA and PUD down, registers kept |
| `nau7802_reg_read` / `nau7802_reg_write` | anything this driver does not model |
| `nau7802_del(h)` | both sections down, device removed, free |

### Start-up

Section 9.1 (p.24): RR to 1, then RR to 0 with PUD to 1; after about 200 µs
PUR reads 1; then configure; nothing converts until CS is set. The driver
gives PUR 200 ms. Its own configuration is PUA and AVDDS (the internal LDO,
when asked) in PU_CTRL, VLDO and GAINS in CTRL1, CRS and CHS in CTRL2, then
the internal offset calibration, then CS.

The datasheet's step 4 example also writes R0x15 = 0x30 (11.10, p.34).
esp-nau7802-loadcell did not, and this driver does not either: there is no
hardware here to check what it changes. `nau7802_reg_write()` reaches it.

### Three tables that are not what they look like

**The rate codes have a hole.** CRS 0 to 3 are 10, 20, 40 and 80 SPS — then
320 SPS is code **7**, with 4, 5 and 6 undefined (11.3, p.30).

**The LDO codes run backwards.** VLDO `000` is the *highest* voltage, 4.5 V,
down to 2.4 V at `111` (11.2, p.29).

**CALMOD is two bits** at the bottom of CTRL2 (11.3, p.30): `00` internal
offset, `10` system offset, `11` system gain, `01` reserved. The internal
trim writes `00` and leaves the rest of CTRL2 alone.

### Registers

Every address and bit in `src/nau7802/nau7802_regs.h` is from the summary
map (p.27) and the register details (section 11, p.28–36): PU_CTRL 11.1,
CTRL1 11.2, CTRL2 11.3, the calibration registers 11.4–11.7 (GCAL's
0x00800000 default meaning unity), ADCO 11.9, REVISION 11.15.3. Standby is
9.1 step 6 (the original cited 9.2). The address 0x2A is on p.15.

## Provenance

- The drivers are ports of esp-hx711-loadcell (a3d509b) and
  esp-nau7802-loadcell (595f278); their logic is unchanged apart from what
  [API usage](usage.md#changes-from-esp-hx711-loadcell-and-esp-nau7802-loadcell)
  lists.
- Every HX711 value is quoted from `docs/hx711.pdf` with its page, table or
  figure, except the 10 µs wake pause marked above. esp-hx711-loadcell had
  no datasheet and cited the Linux driver; the values match.
- Every NAU7802 value is quoted from `docs/nau7802.pdf`.
- Nothing has been checked against a physical chip.
