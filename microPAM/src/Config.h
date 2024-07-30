/* microPAM 
 * Copyright (c) 2023/2024, Walter Zimmer
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
#ifndef CONFIG_H
#define CONFIG_H
  #include <stdint.h>
  
  const char version[] = "\nmicroPAM-V3 build "  __DATE__ " " __TIME__;

  #define START_MODE 0      // -1 is stopped; 0 is closed (ready to open file)

  // for mAcq
  #if defined(AUDIO_INTERFACE)
    #define FSAMP 44100   // for audio interface force 44100
  #else
    #define FSAMP 48000   // sampling frequency
  #endif

  #define NCHAN_I2S  4    // number of I2S channels 
  #define NCHAN_ACQ  1    // number of channels

  #if NCHAN_ACQ == 1
    #define ICH      2    // selected channel (set to -1 to disable monochannel extraction)
  #else
    #define ICH     -1    // disable monochannel extraction
  #endif

  #if NCHAN_ACQ > NCHAN_I2S
    #error "NCHAN_ACQ > NCHAN_I2S"
  #endif

  #if ICH >= NCHAN_I2S
    #error "ICH > NCHAN_I2S-1"
  #endif


  #define MBIT      32      // number of bits / sample from ADC
  #define MDIV       1      // MCLK divider (MCLK = 2*MDIV*BCLK)
  #define NSAMP    128      // number of samples in acq buffer
  #define NBUF_ACQ (NCHAN_ACQ*NSAMP)
  #define NBUF_I2S (NCHAN_I2S*NSAMP)

  #define I2S           0   // I2S microphone
  #define TLV320ADC6140 1   // ADC6140 ADC

//  #define ADC_MODEL I2S
  #define ADC_MODEL TLV320ADC6140

  // for mFiling
  #define MIN_SPACE   2000  // number of disk clusters to keep free
  #define DirPrefix    "T"  // prefix for directory
  #define FilePrefix   "F"  // prefix fir fileName
  #define NBITS         32  // 32,24,16 number of bits in wav file (will be used in saveData)
  #define HourDir        1  // use date/hour/file structure (0 for date/file stucture)

  // for mQueue
//  #define NDBL          12         // number of acuisition buffers fetched from queue for dist storaga
//  #define MAXBUF        (16*NDBL)  // resulting queue length in multiple of disk buffer
  #define NDBL          48         // number of acuisition buffers fetched from queue for dist storaga
  #define MAXBUF        (96*NDBL)  // resulting queue length in multiple of disk buffer
  
  // pocess mode
  #define PROC_MODE      0  // 0: wav data 1; compress 
  #define MB            24  // maximal bits for compression

  // Acq
  #if ADC_MODEL == I2S
    #define AGAIN          0  
    #define DGAIN          0  
  #else
    #define AGAIN         10  // analog gain in dB   // 0:42
    #define DGAIN          0  // digital gain in dB  // (-200:54)/2 
  #endif

  #if PROC_MODE==0
    #if NBITS==32
      #define SHIFT      0       // no shift is needed (32 bit)
    #elif NBITS==24
      #define SHIFT      8       // shift is needed (24 bit)
    #elif NBITS==16
      #define SHIFT      (16+0)  // shift is needed (16 bit) SHIFT<16: amplify
    #else
      #error "no shift defined (config.h)"
    #endif
  #else
    #define SHIFT        (8+4)   // shift data to right to improve compression
  #endif

/******************** Acquisition scheduling *************************/
  #define T_ACQ  20   // duration in sec of each acquisition file
  #define T_ON   60   // duration in sec  of acquisition
  #define T_REP   0   // repetition in sec of acquisition window (0 is continuous)
  #define H_1     0   // start hour of first acquisition block
  #define H_2    12   // stop hour of first acquisition block
  #define H_3    12   // start hour of second acquisition block
  #define H_4    24   // stop hour of second acquisition block
  #define D_ON    1   // duration in days  of acquisition
  #define D_REP   0   // repetition in days of acquisition (0 is continuous)
  #define D_0     0   // start day of acquisition (counted from 1-1-1970 )

  // extern (global) parameters
  extern volatile int32_t fsamp;  // Acq.cpp 
  extern volatile int16_t shift;  // Acq.cpp 
  extern volatile int16_t  proc;  // Acq.cpp 
  extern volatile int16_t again;  // Acq.cpp
  extern volatile int16_t dgain;  // Acq.cpp

  extern volatile uint16_t t_acq; // Filing.cpp 
  extern volatile uint16_t t_on; // Filing.cpp 
  extern volatile uint16_t t_rep; // Filing.cpp 

  extern volatile uint16_t h_1; // Filing.cpp 
  extern volatile uint16_t h_2; // Filing.cpp
  extern volatile uint16_t h_3; // Filing.cpp
  extern volatile uint16_t h_4; // Filing.cpp

  extern volatile uint16_t d_on; // Filing.cpp 
  extern volatile uint16_t d_rep; // Filing.cpp 
  extern volatile  int16_t d_0; // Filing.cpp 

  extern volatile uint16_t *params0; //menu.cpp

  extern class AudioIF acqIF;

  #if defined(__IMXRT1062__)
    #define __not_in_flash_func(func_name) func_name
  #endif

  #define USE_EXT_RTC 0

#endif