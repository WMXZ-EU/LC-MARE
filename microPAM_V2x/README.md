# microPAM_V2
 
 This directory contains the source code for both the Teensy T4.1 and the KB2040/RP2040 based microPAM project that can be found at https://www.micropam.com .
 
 The kb2040/rp2040 code uses both cores with the acquisition running on the second core. This is to ensure that optional data compression has time to run.

 Direct link to hex file (for Teensy 4.1): 
 - https://github.com/WMXZ-EU/microPAM/blob/main/microPAM_V2/build/teensy.avr.teensy41/microPAM_V2.ino.hex

Note for RP2040 models: The sofware uses SdFat by Bill Greiman that is integrated in earlephilhower's RP2040 environment. However, software assumes that ExFat file systems are enabled. There will be an compilation error, if this is not the case. Solution is to open SdFatConfig.h in Arduino's rp2040 environment e.g in C:\Users\...\AppData\Local\Arduino15\packages\rp2040\hardware\rp2040\3.9.3\libraries\ESP8266SdFat\src and change the SDFAT_FILE_TYPE to 3 as suggested by the error message.

 The actual default setting (as of 07-07-2024) are
 - 48 kHz sampling 
 - 20 s wav files (no compression)
 - 32 bit wav file
 - ICS43434 I2S MEMS microphone
 - Store data in //day/hour/file structure 
 
 Actual default settings to be checked in mConfig.h

 In doubt, check/modify mConfig.h and recompile

 To change bit depth of wav files, modify NBITS_WAV in mConfig.h