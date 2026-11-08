microPAM_V3 3.5.13 - user package
=================================
Package of 2026-11-07, built from commit db42d2a.

This package contains the firmware of the LC-MARE passive acoustic recorder (microPAM_V3)
for the Adafruit Feather RP2040 Adalogger and the Adafruit Feather RP2350 HSTX, ready-built
firmware files, and the workbench, a Python program that uploads the firmware, edits the
recorder configuration, talks to the recorder over USB and reads, plots and converts the
recordings. README.md, or README.pdf for reading and printing, describes the recorder, its
configuration and its file format in full. SETUP.md, or SETUP.pdf, is the detailed setup
guide for Windows and macOS, with troubleshooting. WORKBENCH.md, or WORKBENCH.pdf, is the
user guide to the workbench: configuration modes, recording schedules and the four tabs.
LC-Mare-Operations.pdf summarizes the operation of the recorder hardware:
batteries, power jumper and deployment.

Requirements
------------
- Windows 10 or 11.
- Python 3.10 or later, from https://www.python.org (the default installer options include
  tkinter and the 'py' launcher, which are both needed).
- An internet connection for the first setup, which downloads numpy, matplotlib and
  pyserial into a private environment (Python\.venv, about 150 MB). Nothing is installed
  system-wide.
- Only for building the firmware from source: Arduino IDE 2 with the board package
  'Raspberry Pi Pico/RP2040/RP2350' by Earle F. Philhower (see below).

Installation
------------
1. Unzip the package to a folder with a short path, e.g. C:\microPAM_V3 or
   Documents\microPAM_V3; the Python packages contain files whose full names would
   otherwise pass the 260-character limit of Windows. The folder must stay named
   microPAM_V3, because Arduino requires the sketch folder to carry the name of
   microPAM_V3.ino.
2. Double-click setup.cmd. It creates Python\.venv and reports whether Arduino was found.
   setup.cmd -Force (from a command prompt) recreates the environment.
3. Double-click workbench.cmd to start the workbench. If step 2 was skipped, the workbench
   runs the setup itself on its first start.

On macOS, open Terminal in the unzipped folder and run  bash scripts/setup.sh  (with
--force to recreate the environment), then start the workbench with
  Python/.venv/bin/python Python/workbench.py
Python must come from https://www.python.org or from Homebrew together with python-tk.
Uploading .uf2 files works as on Windows; building from source needs the Windows scripts.

Uploading firmware
------------------
The folder firmware\ holds ready-built files for both boards and both recording modes:

  microPAM_V3_3.5.13_rp2040_bin.uf2   RP2040 Adalogger, compressed recording (.bin)
  microPAM_V3_3.5.13_rp2040_wav.uf2   RP2040 Adalogger, raw recording (.wav)
  microPAM_V3_3.5.13_rp2350_bin.uf2   RP2350 HSTX, compressed recording (.bin)
  microPAM_V3_3.5.13_rp2350_wav.uf2   RP2350 HSTX, raw recording (.wav)

Compressed recording stores the same samples losslessly in about half the space; the
workbench (Data tab, To WAV...) converts .bin files to WAV. The files are built with the
defaults of config.h: board default channels, 192 kHz, 60 s files, continuous recording.
These settings, except the recording mode, can be changed afterwards in the workbench
(Configuration tab), which writes /config.txt on the SD card.

In the workbench, select the serial port of the recorder, open the Firmware tab (user mode),
choose a file and press Upload. A file built for the other chip is refused. A board that
does not answer on its serial port can be put into its boot loader by holding BOOT and
pressing RESET; the file is then copied directly.

Building firmware from source (optional)
----------------------------------------
Without Arduino, the developer mode of the Firmware tab is disabled. To enable it:

1. Install Arduino IDE 2 from https://www.arduino.cc/en/software.
2. In File > Preferences, add this URL to 'Additional boards manager URLs':
   https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
3. In Tools > Board > Boards Manager, install 'Raspberry Pi Pico/RP2040/RP2350' by
   Earle F. Philhower (tested with 5.6.0).
4. In the workbench, press 'Check again' on the Firmware tab (or restart it).

A stand-alone arduino-cli on the PATH, or one named by the environment variable
ARDUINO_CLI, is used as well. The settings to compile in are in config.h; developer mode
builds and uploads with the scripts in scripts\ (build.ps1, flash_rp2040.ps1, ...).

Command line
------------
  Python\.venv\Scripts\python.exe Python\bin2wav.py DEST FILE_OR_FOLDER ...
  Python\.venv\Scripts\python.exe Python\uf2_upload.py FILE.uf2 [--port COMn]
