# microPAM V4

Firmware for a low-power **Passive Acoustic Monitor (PAM)** targeting three microcontroller boards. Records multi-channel audio via TDM (Time Division Multiplexed I2S) to SD card, with optional lossless integer compression, FFT-based directional sound intensity estimation, and online VAE-based acoustic classification.

Version: 4.1.0 — Copyright © 2026 Walter Zimmer. Released under the MIT License.

---

## Supported hardware

| Board | MCU | Channels | Max sample rate | DSP / Classifier |
|-------|-----|----------|-----------------|-----------------|
| Teensy 4.1 | i.MX RT1062 | 4 | 192 kHz | Yes (PROC_MODE 2 / 3) |
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

| Value | Mode | Output file | Targets |
|-------|------|-------------|---------|
| 0 | Raw WAV | `.wav` | All |
| 1 | Integer compression (differential + bit-pack) | `.bin` | All |
| 2 | Directional sound intensity (tetrahedral array) + compressed intensity vectors | `.dat` | Teensy 4.1 only |
| 3 | DSP pipeline + 4-VAE classifier — latent means pushed to queue | `.vae` | Teensy 4.1 only |

PROC_MODE 2 and 3 are silently downgraded to 1 on RP2040/RP2350 targets.

---

## Configuration

Edit [`config.h`](config.h) before building:

```c
#define T_ACQ   60    // recording window length (seconds)
#define T_ON     1    // on-time per duty cycle (minutes)
#define T_REP    0    // repeat interval; set < T_ACQ for continuous recording

#define FSAMP  192000 // sample rate (Hz)
#define PROC_MODE  3  // 0 = raw, 1 = compressed, 2 = DSP+intensity, 3 = DSP+VAE latents (T4.1 only)

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
#define AGAIN  20    // ADC analogue gain (dB)
#define DGAIN   0    // ADC digital gain
```

Detection parameters (PROC_MODE 2/3, Teensy 4.1):

```c
#define DETECT_ALPHA  0.01f  // background averaging rate (0.001 slow … 0.1 fast)
#define DETECT_THR    3.0f   // SNR threshold (Dblock / Dmean)
```

---

## DSP pipeline (Teensy 4.1, PROC_MODE 2 / 3)

```
spectrum_apply()     — 50 %-overlap RFFT per channel, Hann window → Z[]
    │
intensity_apply()    — cross-correlation sums → directional intensity I[3×NSAMP]
                     — magnitude per bin → D[NSAMP]
    │
detection_apply()    — peak / mean of D, exponential background tracking, SNR
    │
classifier_trigger() — snapshot D[], pend classifier ISR (IRQ_QTIMER4, priority 128)
    │
(ISR fires asynchronously)
classifier_isr()     — 4 parallel VAE forward + backward passes on D_buf[] quarters
                     — updates weights online (SGD)
                     — exposes vae_mu[VAE_LAT_TOTAL] and classifier_exec_us
                     — PROC_MODE 3: pushes vae_mu as uint32_t onto the queue
```

### VAE classifier (Teensy 4.1 only)

Four parallel online Variational Autoencoders, each trained incrementally on one quarter of the intensity spectrum `D[]`.

| Parameter | Value |
|-----------|-------|
| Number of VAEs | 4 |
| Input per VAE (`VAE_QSAMP`) | `VAE_NSAMP / 4` (= 128 at 192 kHz, 4 ch) |
| Architecture per VAE | `QSAMP – 32 – 2 – 32 – QSAMP` |
| Latent dims per VAE (`VAE_LAT`) | 2 |
| Total exported latent means (`VAE_LAT_TOTAL`) | 8 |
| Learning rate | 1 × 10⁻⁴ (SGD) |
| KL weight β | 1 × 10⁻³ |
| Large weight storage | DMAMEM (RAM2) — W1[4][H][Q] and W4[4][Q][H] |
| IRQ | `IRQ_QTIMER4`, priority 128 |

All 8 latent means are available via `vae_mu[VAE_LAT_TOTAL]` (layout: `[vae0_μ0, vae0_μ1, vae1_μ0, …]`) and printed by the serial monitor each second. In PROC_MODE 3 they are also written to the `.vae` file via the queue.

---

## Architecture

```
Acquisition (DMA IRQ / core 1)
    └─► process()  ──────────────────────────────────────────────────┐
            │  PROC_MODE 0: passthrough → queue                      │
            │  PROC_MODE 1: differential encode + bit-pack → queue   │
            │  PROC_MODE 2: DSP pipeline → compress → queue          │
            │  PROC_MODE 3: DSP pipeline → classifier ISR → queue    │
            └──────────────────────────────────────────────────────► │
                                                                      │
loop() / core 0  ◄────────────────────────────────────────────────────┘
    └─► queue.pull() ──► logger() ──► SD card
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

Output goes to `build\<target>\`:

| Target | Binary | Format |
|--------|--------|--------|
| Teensy 4.1 | `build\teensy\microPAM_V4.ino.hex` | Intel HEX (uploaded via Teensy Loader) |
| RP2040 | `build\rp2040\microPAM_V4.ino.uf2` | UF2 (drag-and-drop or arduino-cli) |
| RP2350 | `build\rp2350\microPAM_V4.ino.uf2` | UF2 (drag-and-drop or arduino-cli) |

> **RP2350 note:** always pass `psram=8mb` (the build script does this automatically). Without it the linker fails because the default `psram_length` is 0.

---

## Flashing (compile + upload)

```powershell
.\scripts\flash_teensy.ps1     # Teensy 4.1 — board must be connected
.\scripts\flash_rp2040.ps1     # RP2040 — normal connection or BOOTSEL+plug
.\scripts\flash_rp2350.ps1     # RP2350 HSTX — normal connection or BOOTSEL+plug
```

RP scripts try two paths in order:
1. BOOTSEL mass-storage drive (`RPI-RP2` / `RP2350`) — copies the `.uf2` directly.
2. Serial port — arduino-cli resets the board into the bootloader.

## Uploading pre-built firmware (upload only)

If you have received pre-built binaries (or already compiled with `build.ps1`) and only need to flash them without recompiling, use the upload-only scripts:

```powershell
.\scripts\upload_teensy.ps1    # Teensy 4.1
.\scripts\upload_rp2040.ps1    # RP2040 Adalogger
.\scripts\upload_rp2350.ps1    # RP2350 HSTX
```

These scripts read the binary directly from `build\<target>\` and abort with a clear message if no pre-built file is found there. The same two-path upload logic applies to the RP boards (BOOTSEL drive first, then serial port).

**Steps to flash a pre-built binary:**

1. Copy the `build\` folder (or the relevant sub-folder) from the source that provided the firmware onto your machine, keeping the path structure intact.
2. Connect the board:
   - **Teensy 4.1**: connect via USB; press the PROGRAM MODE button if prompted during upload.
   - **RP2040 / RP2350**: connect normally, or hold the **BOOTSEL** button while plugging in to force mass-storage mode.
3. Open a PowerShell terminal in the `microPAM_V4` directory and run the appropriate script:
   ```powershell
   .\scripts\upload_teensy.ps1   # or upload_rp2040.ps1 / upload_rp2350.ps1
   ```
4. Wait for `Upload OK`.

---

## Configuration GUI

A Python/tkinter GUI is provided in [`Python/micropam_gui.py`](Python/micropam_gui.py) for configuring the device over USB serial without a terminal.

### Requirements

```
Python 3.x
pyserial
```

A ready-to-use virtual environment is included at `Python/.venv` (Python 3.14, pyserial 3.5). To run from PyCharm, point the interpreter at `Python\.venv\Scripts\python.exe`.

### Running

```powershell
Python\.venv\Scripts\python.exe Python\micropam_gui.py
```

### Features

| GUI element | Serial command | Description |
|---|---|---|
| Port / Connect | — | Lists available COM ports; connects at 115200 baud (USB CDC, baud ignored) |
| UID display | `?u` | Board unique ID, fetched automatically on connect |
| t_acq / t_on / t_rep | `?a` / `!a` … | Recording timing parameters |
| fsamp | `?f` / `!f` | Sampling frequency |
| again | `?g` / `!g` | ADC analogue gain (dB) |
| Artist / Product / Name | `?n` / `!n` … | WAV metadata strings |
| Start Time | `?x` / `!x` | Scheduled start time string |
| h_rec 1–4 | `?1`…`?4` / `!1`…`!4` | Recording hour windows |
| Datetime / getRTC | `?d` | Read current RTC time from MCU into the field |
| Datetime / Set RTC | `!d` | Send the Datetime field value to the MCU RTC |
| Datetime / syncRTC | `!d` | Stamp PC time → field → MCU RTC in one click |
| Read all | all `?` keys | Queries every parameter and populates the GUI |
| Start / Stop | `s` / `e` | Start or stop acquisition |
| Print params | `p` | Print all parameters to the serial monitor |
| Reboot | `b` | Reboot the MCU |
| Hibernate hours / minutes | `x<n>` / `y<n>` | Enter low-power sleep |
| Raw send box | passthrough | Send any command directly |
| Serial monitor | — | Full-width scrolling output, auto-parses `key = value` responses |

---

## Key source files

| File | Role |
|------|------|
| [`config.h`](config.h) | User-facing settings: sample rate, proc mode, metadata |
| [`src/global.h`](src/global.h) | Per-MCU constants: channel counts, buffer sizes, queue depth, gain |
| [`src/process.cxx`](src/process.cxx) | Queue, compression, DSP pipeline, detection, classifier trigger |
| [`src/classifier.cxx`](src/classifier.cxx) | 4 parallel online VAEs: forward pass, backprop, SGD, ISR, queue push (mode 3) |
| [`src/classifier.h`](src/classifier.h) | VAE architecture constants, public API, `vae_mu`, `classifier_exec_us` |
| [`src/adc.cxx`](src/adc.cxx) | TLV320ADC6140 I2C init, gain control |
| [`src/rp2x.cxx`](src/rp2x.cxx) | RP2040/RP2350 PIO TDM, DMA, hibernate, RTC, NeoPixel |
| [`src/Teensy.cxx`](src/Teensy.cxx) | Teensy SAI/I2S, DMA, hibernate (SNVS), UID |
| [`src/filing.cxx`](src/filing.cxx) | SD logger, WAV/bin/dat/vae header writer, config file load/save |
| [`src/rtc.cxx`](src/rtc.cxx) | RV3028 external RTC, internal RTC, time conversion, alarm |
| [`src/menu.cxx`](src/menu.cxx) | Serial menu: start/stop/parameters, `?` query / `!` set protocol |
| [`Python/micropam_gui.py`](Python/micropam_gui.py) | Python/tkinter configuration GUI |

---

## Toolchain version notes

- **Teensy 4.1**: TeensyDuino ≤ 1.61.0 required (as of June 2026).
- **RP2040 / RP2350**: arduino-mbed-rp2040 core 5.6.0 works correctly.
- **Python GUI**: Python 3.x + pyserial 3.5; venv at `Python\.venv`.
