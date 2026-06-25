#ifndef GLOBAL_H
#define GLOBAL_H

  #include "../config.h"

  #define Program "microPAM_V4"
  #define Version "4.0.0" // 18-06-2026

  #define T_4_1   0
  #define RP_2040 1
  #define RP_2350 2

  #if defined __IMXRT1062__
    #define MCU T_4_1
  #elif defined(ARDUINO_ADAFRUIT_FEATHER_RP2040_ADALOGGER)
    #define MCU RP_2040
  #elif defined(ARDUINO_ADAFRUIT_FEATHER_RP2350_HSTX)
    #define MCU RP_2350
  #endif

  #define NBUF 1024

  // for acq

  // for acq/adc hardware
  #if MCU==T_4_1
    #define NPORT_I2S   1
    #define NCHAN_I2S   4
    #define NCHAN_ACQ   4
    #define NBUF_I2S    (2*NBUF)
    #define ADC_SHDNZ   3
    #define ADC_EN      2      
    #define mWire       Wire
    #define USB_POWER   1
    #define MAX_QUEUE   5
    #define USE_EXT_RTC   0
    #if PROC_MODE==2
      #define NAVG      1 // could change this
      #define NCHAN_PROC 3
      #define NBUF_PROC (NCHAN_PROC*NBUF_I2S/NCHAN_I2S)
    #else
      #define NAVG      1
      #define NCHAN_PROC NCHAN_ACQ
      #define NBUF_PROC  NBUF_I2S
    #endif
  
  #elif MCU==RP_2040
    #define NPORT_I2S   1
    #define NCHAN_I2S   1
    #define NCHAN_ACQ   1
    #define NCHAN_PROC  1
    #define NBUF_I2S    NBUF
    #define NBUF_PROC   NBUF
    #define ADC_EN      5
    #define ADC_SHDNZ   6
    #define mWire       Wire
    #define MAX_QUEUE     5
    #define USE_EXT_RTC   1
    #define XRTC_INT_PIN  15
    #define SD_CS         23
  
  #elif MCU==RP_2350
    #define NPORT_I2S   1
    #define NCHAN_I2S   4
    #define NCHAN_ACQ   4
    #define NCHAN_PROC  4
    #define NBUF_I2S    NBUF
    #define NBUF_PROC   NBUF
    #define ADC_EN      5
    #define ADC_SHDNZ   6
    #define mWire       Wire
    #define MAX_QUEUE   225
    #define USE_EXT_RTC   1
    #define XRTC_INT_PIN  29
    #define SD_CS         25 
  #endif

  // limit processing mode for other than Teensy4.1
  #if MCU != T_4_1
    #if PROC_MODE==2
      #undef PROC_MODE
      #define PROC_MODE 1
    #endif
    #define NAVG        1
    #define USB_POWER   0
  #endif

  // limit sampling frequency of ADC
  #if NCHAN_I2S>3
    #if FSAMP>192000
      #undef FSAMP
      #define FSAMP 192000
    #endif
  #endif
  //
  #if MCU==RP_2040
    #if FSAMP>192000
      #undef FSAMP
      #define FSAMP 192000
    #endif
  #endif

  // for adc
  #define AGAIN 20
  #define DGAIN 0

  // for processing
  #define SHIFT 12
  #if NCHAN_ADC < NCHAN_I2S
    #define ICH 0
  #endif

  // for filing (and queue)
  #define NBUF_DISK (8*NBUF_I2S)

  // for XRTC
  #define XRTC_SDA   2
  #define XRTC_SCL   3

  // program states
  enum status_t  {DO_START, CLOSED, RECORDING, MUST_STOP, JUST_STOPPED, STOPPED, MUST_HIBERNATE};
  extern char status_text[][16];

  // setting MCU clock speed according to sampling frequency
  #if FSAMP<=96000
    #define CLK_MULT 4  // (48 MHz)
  #elif FSAMP<=192000
    #define CLK_MULT 8  // (96 MHz)
  #else
    #define CLK_MULT 12 // (144 MHz)
  #endif

  #include <cstdint>

  // in rp2040.cxx/Teensy.cxx
  extern volatile uint32_t fsamp;  // sampling frequency (kHz) needed inter alia for filing

  #define MBIT 32

  #if MCU==T_4_1
    #define __not_in_flash_func(func) func
  #endif

  #define MONITOR 1
#endif