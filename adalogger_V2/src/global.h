/* microPAM 
 * Copyright (c) 2023/2024/2025, Walter Zimmer
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
  #define Version "2.0.x.1" 
  #define PreAmp  0                   // 0: CMOS; 1: FET; 2 Mark
  #define Program "Adalogger_V2a"

  #define WAIT      0.5     // seconds to wait for serial (0 do not wait)

  // definitions for acquisition and filing
  #define NCHAN_I2S   1   // controls the I2S interface
  #define NCH         1   // for wav header (Mono or stereo)

  #define PROC      0     // 0 Wav file; 1 compress (not aproved yet, so should be 0)

  #define MBIT      32
  #define MBUF      (8*6)           // should be a multople of 6
  #if PROC==0
    #define NBUF_I2S  (MBUF/2*1024) // actual buffer length in samples for acquisition and filing (dual buffer)
  #else
    #define NBUF_I2S  (MBUF/3*1024) // actual buffer length in samples for acquisition and filing (triple buffer)
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
  #define XRTC_INT_PIN 15     // V2

  // program states
  enum status_t  {DO_START, CLOSED, RECORDING, MUST_STOP, JUST_STOPPED, STOPPED};

  // RP2040 specific
  #define MC 1 // use second core for acquisition 

  // in filing.cxx
  extern uint32_t t_acq;  // seconds (each file)
  extern uint32_t t_on;   // minutes (each on period)
  extern uint32_t t_rep;  // minutes (for continuous recording set t_rep < t_acq)
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