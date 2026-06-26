# microPAM V4

Firmware for a low-power **Passive Acoustic Monitor (PAM)** targeting three microcontroller boards. Records multi-channel audio via TDM (Time Division Multiplexed I2S) to SD card, with optional lossless integer compression, FFT-based directional sound intensity estimation, and online VAE-based acoustic classification.

Version: 4.1.0 — Copyright © 2026 Walter Zimmer. Released under the MIT License.

---

## Supported hardware

| Board | MCU | Channels | Max sample rate | DSP / Classifier |
|-------|-----|----------|-----------------|-----------------|
| Teensy 4.1 | i.MX RT1062 | 4 | 192 kHz | Yes (PROC_MODE 2) |
| Adafruit Feather RP2040 Adalogger | RP2040 | 1 | 192 kHz | No |
| Adafruit Feather RP2350 HSTX | RP2350 | 4 | 192 kHz | No |

All boards use a **TLV320ADC6140** analog front end (I2C configuration, programmable gain). The RP2350 board stores its deep DMA queue in 8 MB of on-board PSRAM.

ADC channel-count limits (hardware):

| `NCHAN_I2S` | Max `FSAMP` |
|-------------|-------------|
| 1 | 192 kHz |
| 2 | 384 kHz |
| > 2 | 192 kHz |

---

## Processing modes

Set `PROC_MODE` in [`config.h`](config.h):

| Value | Mode | Scaling | Targets |
|-------|------|---------|---------|
| 0 | Raw WAV | MSB = Vref | All |
| 1 | Integer compression (differential + bit-pack) | MSB = Vref × (1 << SHIFT) | All |
| 2 | Directional sound intensity (tetrahedral array) + VAE classifier | — | Teensy 4.1 only |

PROC_MODE 2 is silently downgraded to 1 on RP2040/RP2350 targets.

---

## Configuration

Edit [`config.h`](config.h) before building:

```c
#define T_ACQ   60    // recording window length (seconds)
#define T_ON     1    // on-time per duty cycle (minutes)
#define T_REP    0    // repeat interval; set < T_ACQ for continuous recording

#define FSAMP  192000 // sample rate (Hz)
#define PROC_MODE  2  // 0 = raw, 1 = compressed, 2 = DSP + classifier (T4.1 only)

// WAV file metadata
#define SRC_str "LC17"      // source/instrument ID
#define CMS_str "WMXZ"      // organisation
#define ART_str "WMXZ"      // creator
#define PRD_str "microPAM"  // product / activity
#define SBJ_str "______"    // area
#define NAM_str "Test"      // location ID
```

Key DSP/gain constants in [`src/global.h`](src/global.h):

```c
#define SHIFT  12    // bit-shift for integer compression
#define PGAIN  12    // additional power gain exponent applied to intensity output
                     // scale2 = 1 << (31 - SHIFT + PGAIN)
#define AGAIN  20    // ADC analogue gain (dB)
#define DGAIN   0    // ADC digital gain
```

Detection parameters (PROC_MODE 2, Teensy 4.1):

```c
#define DETECT_ALPHA  0.01f  // background averaging rate (0.001 slow … 0.1 fast)
#define DETECT_THR    3.0f   // SNR threshold (Dblock / Dmean)
```

---

## DSP pipeline (Teensy 4.1, PROC_MODE 2)

```
spectrum_apply()   — 50 %-overlap RFFT per channel, window × FFT → Z[]
    │
intensity_apply()  — cross-correlation sums → directional intensity I[3×NSAMP]
                   — magnitude per bin → D[NSAMP]
    │
detection_apply()  — peak / mean of D, exponential background tracking, SNR
    │
classifier_trigger() — snapshot D[], pend classifier ISR (IRQ_QTIMER4, priority 128)
    │
(ISR fires asynchronously)
classifier_isr()   — VAE forward + backward pass on D_buf[], updates weights online
                   — exposes vae_mu[VAE_LAT] and classifier_exec_us
```

### VAE classifier (Teensy 4.1 only)

Online Variational Autoencoder trained incrementally on each processed block.

| Parameter | Value |
|-----------|-------|
| Architecture | `NSAMP – VAE_H1 – VAE_LAT – VAE_H1 – NSAMP` |
| `NSAMP` | `NBUF_I2S / NCHAN_ACQ` (= 512 at 192 kHz, 4 ch) |
| `VAE_H1` | 32 |
| `VAE_LAT` | 4 |
| Learning rate | 1 × 10⁻⁴ (SGD) |
| KL weight β | 1 × 10⁻³ |
| Large weight storage | DMAMEM (RAM2) — W1 and W4, ~128 KB each |
| IRQ | `IRQ_QTIMER4`, priority 128 |

The 4 latent means are available via `vae_mu[VAE_LAT]` and the last ISR execution time via `classifier_exec_us` (µs).

---

## Architecture

```
Acquisition (DMA IRQ / core 1)
    └─► process()  ──────────────────────────────────────────────┐
            │  PROC_MODE 0: passthrough                           │
            │  PROC_MODE 1: differential encoding + bit-pack      │
            │  PROC_MODE 2: DSP pipeline (T4.1, see above)        │
            └─► queue.push()                                      │
                                                                  │
loop() / core 0  ◄────────────────────────────────────────────────┘
    └─► queue.pull() ──► logger() ──► SD card (WAV)
```

- **RP2040 / RP2350**: acquisition on core 1 (`core1_main`); filing, RTC, and serial menu on core 0.
- **Teensy 4.1**: single-core; DMA ISR drives `process()` directly.
- The RP2350 queue (`MAX_QUEUE = 225`) is placed in PSRAM via a linker section attribute.

---

## Building

Requires [arduino-cli](https://arduino.github.io/arduino-cli/) installed at:

```
%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
```

Config directory: `%LOCALAPPDATA%\Arduino15`

```powershell
.\scripts\build.ps1            # all three targets
.\scripts\build.ps1 teensy     # Teensy 4.1 only
.\scripts\build.ps1 rp2040     # RP2040 Adalogger only
.\scripts\build.ps1 rp2350     # RP2350 HSTX only
```

Output goes to `build\<target>\`.

> **RP2350 note:** always pass `psram=8mb` (the build script does this automatically). Without it the linker fails because the default `psram_length` is 0.

---

## Flashing

```powershell
.\scripts\flash_teensy.ps1     # Teensy 4.1 — board must be connected
.\scripts\flash_rp2040.ps1     # RP2040 — normal connection or BOOTSEL+plug
.\scripts\flash_rp2350.ps1     # RP2350 HSTX — normal connection or BOOTSEL+plug
```

RP scripts try two paths in order:
1. BOOTSEL mass-storage drive (`RPI-RP2` / `RP2350`) — copies the `.uf2` directly.
2. Serial port — arduino-cli resets the board into the bootloader.

---

## Key source files

| File | Role |
|------|------|
| [`config.h`](config.h) | User-facing settings: sample rate, proc mode, metadata |
| [`src/global.h`](src/global.h) | Per-MCU constants: channel counts, buffer sizes, queue depth, gain |
| [`src/process.cxx`](src/process.cxx) | Queue, compression, DSP pipeline, detection, classifier trigger |
| [`src/classifier.cxx`](src/classifier.cxx) | Online VAE: forward pass, backprop, SGD weight update, ISR |
| [`src/classifier.h`](src/classifier.h) | VAE architecture constants, public API, `vae_mu`, `classifier_exec_us` |
| [`src/adc.cxx`](src/adc.cxx) | TLV320ADC6140 I2C init, gain control |
| [`src/rp2x.cxx`](src/rp2x.cxx) | RP2040/RP2350 PIO TDM, DMA, hibernate, RTC, NeoPixel |
| [`src/Teensy.cxx`](src/Teensy.cxx) | Teensy SAI/I2S, DMA, hibernate (SNVS), UID |
| [`src/filing.cxx`](src/filing.cxx) | SD logger, WAV header writer, config file load/save |
| [`src/rtc.cxx`](src/rtc.cxx) | RV3028 external RTC, time conversion, alarm |
| [`src/menu.cxx`](src/menu.cxx) | Serial menu: start/stop/parameters |

---

## Toolchain version notes

- **Teensy 4.1**: TeensyDuino ≤ 1.61.0 required (as of June 2026).
- **RP2040 / RP2350**: arduino-mbed-rp2040 core 5.6.0 works correctly.
