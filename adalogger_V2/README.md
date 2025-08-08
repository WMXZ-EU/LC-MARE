# adalogger
 This directory contains the source code for RP2040 based Adalogger project that 
 can be found at https://www.micropam.com .

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
  program developed for TMS320ADC6140 ADC, but derived from program for MEMS I2S microphone 
  external RTC is RV2038
  integer compression (code is present but not finally tested)

## UF2 file
 Direct link to uf2 file: 
 - https://github.com/WMXZ-EU/microPAM/blob/main/adalogger_V2/build/rp2040.rp2040.adafruit_feather_adalogger/adalogger_V2.ino.uf2
 
 ## 