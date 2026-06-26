# microPAM_V4

Firmware for a Passive Acoustic Monitor (PAM). Records multi-channel audio via TDM (not standard I2S) to SD card with optional FFT-based directional sound intensity estimation.

## Targets

| Board | FQBN | Notes |
|-------|------|-------|
| Teensy 4.1 | `teensy:avr:teensy41` | Dual preamps, 4-ch, DSP (PROC_MODE 0/1/2/3) |
| RP2040 Adafruit Feather Adalogger | `rp2040:rp2040:adafruit_feather_adalogger` | Single preamp, 1-ch, no DSP |
| RP2350 Adafruit Feather HSTX | `rp2040:rp2040:adafruit_feather_rp2350_hstx:psram=8mb` | Dual preamps, 4-ch, **must pass psram=8mb** or linker fails |

## Compile

```powershell
.\scripts\build.ps1            # all three targets
.\scripts\build.ps1 teensy     # Teensy 4.1 only
.\scripts\build.ps1 rp2040     # RP2040 only
.\scripts\build.ps1 rp2350     # RP2350 only
```

Output goes to `build\<target>\`.

## Compile and upload

```powershell
.\scripts\flash_teensy.ps1     # Teensy 4.1  — board must be connected
.\scripts\flash_rp2040.ps1     # RP2040      — connected normally, or hold BOOTSEL while plugging in
.\scripts\flash_rp2350.ps1     # RP2350 HSTX — connected normally, or hold BOOTSEL while plugging in
```

RP scripts try two upload paths in order:
1. BOOTSEL mass-storage drive (`RPI-RP2` / `RP2350`) — copies `.uf2` directly.
2. Serial port — arduino-cli resets the board into bootloader.

## Key source files

| File | Role |
|------|------|
| `config.h` | User-facing config: sample rate, proc mode, file metadata |
| `src/global.h` | Per-MCU constants: channel counts, buffer sizes, queue depth |
| `src/process.cxx` | Queue, compression (encodeData/encodeBlock), DSP (T4.1 only) |
| `src/classifier.cxx` | 4 parallel online VAEs (forward, backprop, SGD); ISR pushes latents to queue in mode 3 |
| `src/classifier.h` | VAE architecture constants (`VAE_NVAE`, `VAE_QSAMP`, `VAE_LAT`, `VAE_LAT_TOTAL`), `vae_mu[]` |
| `src/adc.cxx` | TLV320ADC6140 I2C init, gain control |
| `src/rp2x.cxx` | RP2040/RP2350 PIO TDM, DMA, hibernate, RTC, NeoPixel |
| `src/Teensy.cxx` | Teensy SAI/I2S, DMA, hibernate (SNVS), uid |
| `src/filing.cxx` | SD card logger, WAV/bin/dat/vae header, config file load/save |
| `src/rtc.cxx` | RV3028 external RTC, internal RTC, time conversion, alarm |
| `src/menu.cxx` | Serial menu: start/stop/parameters, `?` query / `!` set protocol |
| `Python/micropam_gui.py` | Python/tkinter configuration GUI (USB serial, pyserial) |

## Architecture

- **RP2040/RP2350**: acquisition runs on core 1 (`core1_main`), filing/menu on core 0. DMA IRQ copies I2S buffer then calls `process()`.
- **Teensy 4.1**: single-core, DMA ISR calls `process()` directly.
- `process()` → compresses or DSP-transforms the buffer → `queue.push()`.
- `loop()` → `queue.pull()` → `logger()` writes to SD.

## Processing modes (`PROC_MODE` in config.h)

| Value | Mode | Targets |
|-------|------|---------|
| 0 | Raw WAV | All |
| 1 | Integer compression (differential + bit-pack) | All |
| 2 | DSP: directional sound intensity (tetrahedral array) + compressed intensity vectors | T4.1 only (forced to 1 on RP) |
| 3 | DSP + 4-VAE classifier: 8 latent means (float as uint32_t) pushed to queue → `.vae` file | T4.1 only (forced to 1 on RP) |

## Queue / PSRAM

- `MAX_QUEUE` controls queue depth. On RP2350 with `MAX_QUEUE > 5` the buffer is placed in PSRAM via the `PSRAM` section attribute.
- The RP2350 HSTX board **default psram_length is 0** — always pass `psram=8mb` when compiling, or the linker fails.
- On RP2040 `MAX_QUEUE=5` (SRAM only); RAM usage is ~87%, which is acceptable.

## arduino-cli location

```
%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
```

Config dir: `%LOCALAPPDATA%\Arduino15`

## Python GUI

```powershell
Python\.venv\Scripts\python.exe Python\micropam_gui.py
```

- Requires pyserial (pre-installed in `Python\.venv`).
- PyCharm interpreter: `Python\.venv\Scripts\python.exe`.
- Communicates over USB serial using the `?` / `!` menu protocol from `src/menu.cxx`.
