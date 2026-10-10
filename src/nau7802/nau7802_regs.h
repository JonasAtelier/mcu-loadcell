/*
 * NAU7802 registers and bit fields.
 *
 * Every value here is transcribed from Nuvoton's own datasheet, revision 1.7,
 * included as docs/nau7802.pdf -- the summary register map (section 10,
 * p.27) and the register map details (section 11, p.28-36), with the
 * section named on each block. No other driver was consulted.
 */
#pragma once

/* "The NAU7802 is permanently programmed with 010 1010 (0x2A) as the Device
 * Address" (p.15) -- there is no address pin, so one part per bus. */
#define NAU7802_ADDR        0x2A

#define NAU7802_PU_CTRL     0x00
#define NAU7802_CTRL1       0x01
#define NAU7802_CTRL2       0x02
#define NAU7802_OCAL1_B2    0x03   /* channel 1 offset calibration, 24 bit */
#define NAU7802_GCAL1_B3    0x06   /* channel 1 gain calibration, 32 bit */
#define NAU7802_OCAL2_B2    0x0A   /* channel 2 */
#define NAU7802_GCAL2_B3    0x0D
#define NAU7802_I2C_CTRL    0x11
#define NAU7802_ADCO_B2     0x12   /* conversion result, 24 bit, read only */
#define NAU7802_OTP_B1      0x15
#define NAU7802_REVISION    0x1F   /* revision code, read only, 11.15.3 */

#define NAU7802_ADC_BYTES   3      /* 0x12..0x14, 11.9 p.33 */

/* ---- PU_CTRL (0x00), default 0x00, 11.1 p.28 ---- */
#define NAU7802_RR          0x01   /* register reset, level triggered */
#define NAU7802_PUD         0x02   /* power up digital */
#define NAU7802_PUA         0x04   /* power up analog; PUD must be 1 too */
#define NAU7802_PUR         0x08   /* power up ready, read only */
#define NAU7802_CS          0x10   /* cycle start */
#define NAU7802_CR          0x20   /* cycle ready, read only: data available */
#define NAU7802_OSCS        0x40   /* 1 = external crystal */
#define NAU7802_AVDDS       0x80   /* 1 = internal LDO, 0 = AVDD pin */

/* ---- CTRL1 (0x01), default 0x00, 11.2 p.29 ---- */
#define NAU7802_GAINS_SHIFT 0      /* bits 2:0 */
#define NAU7802_VLDO_SHIFT  3      /* bits 5:3 */
#define NAU7802_DRDY_SEL    0x40
#define NAU7802_CRP         0x80

/* ---- CTRL2 (0x02), default 0x00, 11.3 p.30 ---- */
#define NAU7802_CALMOD_SHIFT 0     /* bits 1:0 */
#define NAU7802_CALMOD_MASK  (0x03 << NAU7802_CALMOD_SHIFT)
#define NAU7802_CALS         0x04  /* write 1 to start; reads 1 while running */
#define NAU7802_CAL_ERR      0x08  /* read only, 1 = the calibration failed */
#define NAU7802_CRS_SHIFT    4     /* bits 6:4 */
#define NAU7802_CHS          0x80  /* 1 = channel 2 */

/* CALMOD: what a CALS-triggered calibration actually does. */
#define NAU7802_CALMOD_INTERNAL_OFFSET 0x00   /* default */
#define NAU7802_CALMOD_SYSTEM_OFFSET   0x02
#define NAU7802_CALMOD_SYSTEM_GAIN     0x03

/* Reset default of the gain calibration registers, from the summary map
 * (p.27) and 11.7 (p.31): GCALn_B2 is 0x80 and the rest 0x00, i.e.
 * 0x00800000 meaning unity. */
#define NAU7802_GCAL_UNITY  0x00800000UL
