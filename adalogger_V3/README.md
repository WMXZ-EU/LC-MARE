# adalogger
 This directory contains the source code for RP2040 based Adalogger project that 
 can be found at https://www.micropam.com . The code is also the baseis of the LC-MARE project.

 The menu is minimized to start/end acquisition, correct external RTC and to print actual acquisition parameters. It is expected that the user adapts the acquisition parameters in config.h and recompiles the program.

## Menu
 The menu commands are 
 - s: start acquisition
 - e: end acquisition
 - c: check and change RTC
 - p: print system and acquisition parameters 
 - x: reboot system (adding to x a number hibernates the system [eg, x 1 hibernates and restarts at the next full hour] )

    parameter settings
 - !a xx: set squisition (file length) to xx seconds
 - !o x: set on time to x minutes (sic!)
 - !r x: set repetition rate to x minutes (sic!) continuous if "rep rate" < "on time"
 - !f x: set sampling frequency in Hz
 - !g x: set analog gain in dB
 - !w x: set eeprom mode to x (0: stored but not to be used, 1: stored and to be used on reboot)
 
 ## Run configuration
 The run configuration is controlled by config.h

 ## Sytem configuration
 System configuration is controlled by src/global.h

## Miscalaneous
  - program developed for TMS320ADC6140 ADC, but derived from program for MEMS I2S microphone 
  - external RTC is RV2038
  - integer compression (PROC = 1)

## (main) differences to adalogger_V2
  - Using of queue to bridge miscroSD card latencies
  - compression (PROC=1) is carried out on acq core (core #1)

## UF2 file
 Direct link to uf2 file: 
 
 For Adalogger
 - https://github.com/WMXZ-EU/microPAM/blob/main/adalogger_V3/build/rp2040.rp2040.adafruit_feather_adalogger/adalogger_V3.ino.uf2
 For RP2350 Feather
 - https://github.com/WMXZ-EU/microPAM/blob/main/adalogger_V3/build/rp2040.rp2040.adafruit_feather_rp2350_hstx/adalogger_V3.ino.uf2
 
 ## Upgrade firmware
 - copy UF2 file (e.g. as above from Github) to local PC
 - disconnect battery 
 - connect usb to PC
 - Press boot button (furthest button from usb connector)
 - while pressed, press shortly reset button (closest button from usb connector)
 - release boot button
 - on PC there should be a new disk showing up call uf2
 - if not automatically opened, open this disk
 - copy/drag UF2 file into this disk/folder
 - once the file is completely copied, folder/disk will disappear and mcu will restart with new firmware