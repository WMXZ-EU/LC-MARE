# micoPAM_V4
This is a development project to generate firmware for rp2040 Adalogger, rp2350 feather, and Teenys 4.1

The acquisition module is tuned for TDM and does not follow the I2S protocol.

Extension in processing is planned for Teensy 4.1 MCU 
On T4.1 implemented: directional sound intensity estimation

Note: 
For Teensy 4.1: For the time being needs TeensyDuino up to 1.61.0 (as of 23-06-2026)
For RP2440/RP2350: Seems fine with 5.6.0

open terminal in filder and type
claude "continue working on microPAM_V4"

scalings:
proc_mode=0: raw data, no scaling: MSB = Vref 
proc_mode=1: integer copression, scaling: MSB = Vref*(1<<shift)
pric_mode=2: intensity estimation, scaling: 