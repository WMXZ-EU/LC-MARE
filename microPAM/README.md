# microPAM_V3
 
 This directory contains the source code for both the Teensy T4.1 and the KB2040/RP2040 based microPAM project that 
 can be found at https://www.micropam.com .
 
 The kb2040/rp2040 code uses both cores with the acquisition running on the second core. 
 This is to ensure that optional data compression has time to run.

 Direct link to hex file (for Teensy 4.1): 
 - https://github.com/WMXZ-EU/microPAM/blob/main/microPAM_V3/build/teensy.avr.teensy41/microPAM_V3.ino.hex (Arduino build)
 - https://github.com/WMXZ-EU/microPAM/blob/main/microPAM_V3/.vsteensy/build/microPAM_V3_T41.hex (VSCode build)

 The actual default setting (as of 16-06-2023) are
 - I2S 
 - 1 min wav files (no compression)
 - 48 kHz sampling 
 - 24 bit wav file
 - Store data in //day/hour/file structure 

Check actual configuration by inspecting Config.h

 The V3 version contains additionally an ADC interface to TLV320ADC and is modified to address the needs for "microPAM-mare" implementation.

 The program may be compiled either using Arduino IDE or via makefile. The environment is prepared for Arduini IDE and Visual Studio Code. For Visual Studio Code, the following extension are used; C/C++ (including IntelliSense) and Tasks (to load VSCode tasks into Status Bar)
The Makefile is only available for Teensy models. For the RP2040 the Arduino Environment must be used.

 ## Configuration
 The program configuration is controlled by Config.h