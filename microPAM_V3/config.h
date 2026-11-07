/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
// User-facing recording settings. Everything hardware-dependent is derived in src/global.h.
// Values set here are defaults; /config.txt on the SD card overrides them at boot.
#ifndef CONFIG_H
#define CONFIG_H

  //------------------------------------
  // carrier board (Teensy 4.1 only; RP2040/RP2350 have a fixed pinout and ignore it)
  //------------------------------------
  //   HW_LC_MARE4  LC_Mare4: ADC EN/SHDNZ on pins 2/3
  //   HW_CVAS3     cVAS3:    ADC EN/SHDNZ on pins 32/33, digital-supply LDO on pin 31
  #define HW_LC_MARE4 0
  #define HW_CVAS3    1
  #define HW_BOARD  HW_LC_MARE4

  //------------------------------------
  // processing mode
  //------------------------------------
  #define PROC_MODE 1   // 0: raw WAV (.wav); 1: compressed raw (.bin)

  //------------------------------------
  // duty cycle
  //------------------------------------
  #define T_ACQ     60  // seconds per file
  #define T_ON      1   // minutes on per repetition period
  #define T_REP     0   // minutes per repetition period (continuous recording if T_REP <= T_ON)

  //------------------------------------
  // acquisition
  //------------------------------------
  #define FSAMP 192000  // Hz; capped at 192 kHz on Teensy 4.1, 384 kHz on RP2040 and RP2350 (192 kHz with NCHAN 4)
  #define NCHAN     0   // 0: board default (1 channel on RP2040, 2 on RP2350 and Teensy 4.1); 4: all four ADC inputs
  #define NB_MIN    0   // PROC_MODE 1: minimum bit width nb of the packed differences, for tests
                        // with open inputs (data volume as with a signal); 0: off (normal)

  //------------------------------------
  // microSD
  //------------------------------------
  #define SD_CMD7   1   // card to stand-by between block writes; 0: off. Teensy 4.1 (SDIO): CMD7
                        // deselect/select. RP (SPI): syncDevice() and CMD7 (RCA 0), which
                        // the SD specification does not support in SPI mode (R1 at boot).
                        // RP2040 SDIO: CMD7 as on the Teensy if SdFat is patched
                        // (scripts/patch_sdfat.ps1), else syncDevice() only
  #define SD_SDIO   1   // RP2040 Adalogger only: 1: 4-bit PIO SDIO (SdFat, GPIO 18-23); 0: SPI

  //------------------------------------
  // power (Teensy 4.1 only)
  //------------------------------------
  #define PHY_OFF   0   // 1: Ethernet PHY held in power-down and reset; 0: left as after boot
                        // (measured stopped: 40 mA with 0, 43 mA with 1, so 0 is kept)
  #define ADC_PWR   1   // 0: ADC board (USB host 5 V) never powered, for current tests only
  #define T41_POWER 6  // sum of: 1 unused PLLs off; 2 core from 24 MHz crystal, ARM PLL off
                        // (speed=24 only); 4 clock gates of unused peripherals closed.
                        // Menu 'z<n>' applies them at run time ('z0': clock report).
                        // Measured stopped: 41 mA, with 2: 39 mA, with 2+4: 38 mA; recording
                        // 192 kHz 88 mA (91 mA without). 1 has no effect (PLLs already off).

  //------------------------------------
  // file metadata (WAV LIST/INFO chunk)
  //------------------------------------
  // 'Source' as <name>-<C0>: C0 is the level in dB re 1 uPa of one 24-bit ADC count at
  // 0 dB analog gain (Python/micropam_calib.py). LC17 = SQ17-00 on the LC-Mare front end,
  // from the resistor measurement of unit 1B843C2E (42.8 dB at 20 dB gain and SHIFT 12)
  #define SRC_str "LC17-38.7"
  #define CMS_str "WMXZ"     // 'Commissioned' (organization)
  #define ART_str "WMXZ"     // 'Artist'  (creator)
  #define PRD_str "microPAM" // 'Product' (project)
  #define SBJ_str "______"   // 'Subject' (area)
  #define NAM_str "Test"     // 'Name'    (location id)

#endif
