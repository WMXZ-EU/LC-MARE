#ifndef GLOBAL_H
#define GLOBAL_H

  #include "../config.h"

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
  #define NBUF_I2S NBUF

  // for acq/adc hardware
  #if MCU==T_4_1
    #define NPORT_I2S   1
    #define NCHAN_I2S   2
    #define NCHAN_ACQ   2
    #define ADC_SHDNZ   3
    #define ADC_EN      2      
    #define mWire       Wire
    #define USB_POWER   1
    #define MAX_QUEUE   320
    #define USE_EXT_RTC   0
  
  #elif MCU==RP_2040
    #define NPORT_I2S   1
    #define NCHAN_I2S   1
    #define NCHAN_ACQ   1
    #define ADC_EN      5
    #define ADC_SHDNZ   6
    #define mWire       Wire
    #define USB_POWER   0
    #define MAX_QUEUE     5
    #define USE_EXT_RTC   1
    #define XRTC_INT_PIN  15
  
  #elif MCU==RP_2350
    #define NPORT_I2S   1
    #define NCHAN_I2S   2
    #define NCHAN_ACQ   2
    #define ADC_EN      5
    #define ADC_SHDNZ   6
    #define mWire       Wire
    #define USB_POWER   0
    #define MAX_QUEUE   225
    #define USE_EXT_RTC   1
    #define XRTC_INT_PIN 29
  #endif

  // for adc
  #define AGAIN 20
  #define DGAIN 0

  // for processing
  #define SHIFT 12
  #if NCHAN_ADC < NCHAN_I2S
    #define ICH 0
  #endif

  // for queue
  #define NBLOCK (8*NBUF)
  #define NBUF_DISK NBLOCK

  // for XRTC
  #define XRTC_SDA   2
  #define XRTC_SCL   3

#include <cstdint>
#endif