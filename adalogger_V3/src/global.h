/* microPAM 
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
#ifndef GLOBAL_H
#define GLOBAL_H

#include "../config.h"
  #define Program "Adalogger_V3a"
  #define Version "3.0.2" // 10-05-2026
  #define PreAmp  0       // 0: CMOS; 1: FET; 2 Mark

  // hardware (so far descriptive only)
  #define PREAMP CMOS
  #define ADC ADC_V2
  #define RTC RV_3028_PIMORONI
  #define BASE LC_MARE_09_2025

  #define WAIT      5     // seconds to wait for serial (0 do not wait)

  #if defined(ARDUINO_ADAFRUIT_FEATHER_RP2040_ADALOGGER)
    #define MCU RP2040
  #elif defined(ARDUINO_ADAFRUIT_FEATHER_RP2350_HSTX)
    #define MCU RP22350
  #endif

  // definitions for acquisition and filing
  #if MCU==RP240
    #define NCHAN_I2S   1   // controls the I2S interface
    #define NCH         1   // for wav header (Mono or stereo)
    #if FSAMP>96000         // limit sampling frequency
      #undef FSAMP
      #define FSAMP 96000
    #endif
    //
  #elif MCU==RP2350
    #define NCHAN_I2S   2   // controls the I2S interface
    #define NCH         2   // for wav header (Mono or stereo)
    #if FSAMP>384000        // limit sampling frequency
      #undef FSAMP
      #define FSAMP 384000
    #endif
  #endif

  #define MBIT      32    // number of bits in I2S

  #define NBUF_I2S    1024  // ADC
  #define NBUF_ACQ    1024  // Acquisition

  #define MD 8        // multiplier for definition of disk block (also on queue)

  #if defined(RP2350_PSRAM_CS)
    #define MAX_QUEUE (225) // 4*225*8*1024 = 7200*1024 or 7.2 MB (8 MB PSRAM)
  #else
    #define MAX_QUEUE (5)   // 4*5*8*1024 = 160*1024    or 160 kB
  #endif
  
  #if PROC==0
    #define SHIFT (0)
  #elif PROC==1
    #define SHIFT (8+4)
  #endif

  // Acoustic sensor
  #define MEMS 0
  #define TLV320ADC6140 1

  #define ADC_MODEL TLV320ADC6140

  #if ADC_MODEL==TLV320ADC6140
    #if PreAmp==0
      #define AGAIN 20
    #else
      #define AGAIN 0
    #endif
    #define DGAIN 0
  #endif

  //#define XRTC_INT_PIN A2   // DS3231 Feather Wing
  #define XRTC_INT_PIN 15     // V2,V3

  // program states
  enum status_t  {DO_START, CLOSED, RECORDING, MUST_STOP, JUST_STOPPED, STOPPED};
  extern char status_text[][16];
  
  // RP2040 specific
  #define MC 1 // use second core for acquisition 

  // setting MCU clock speed according to sampling frequency
  #if FSAMP<=96000
    #define CLK_MULT 4  // (48 MHz)
  #elif FSAMP<=192000
    #define CLK_MULT 8  // (96 MHz)
  #else
    #define CLK_MULT 12 // (144 MHz)
  #endif

  #define SD_MULT   6   // (72 MHz) (is a littlt bit high, but seems to work)
  #define USE_SDIO  0   // use spi to reduce power consumption 
  
  // in filing.cxx
  extern uint16_t t_acq;   // seconds (each file)
  extern uint16_t t_on;    // minutes (each on period)
  extern uint16_t t_rep;   // minutes (for continuous recording set t_rep < t_acq)
  extern uint16_t h_rec[]; // hours for selective recordings {0,12,12,24}

  // in rp2040.cxx
  extern uint32_t fsamp;  // sampling frequency (kHz)
  // in Adc.cxx
  extern uint32_t again;  // ADC gain

  extern uint32_t alarm;  // initial wakeup time

  // in filing
  extern char ISRC[]; //  Source
  extern char ICMS[]; //  Organization
  extern char IART[]; // 'Artist' (creator)
  extern char IPRD[]; // 'Product' (Activity)
  extern char ISBJ[]; // 'subject' (Area)
  extern char INAM[]; // 'Name' (location id)

#endif