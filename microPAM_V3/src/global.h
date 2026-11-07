/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
// Hardware-dependent definitions derived from config.h.
#ifndef GLOBAL_H
#define GLOBAL_H

#include <stdint.h>
#include "../config.h"

  #define Program "microPAM_V3"
  #define Version "3.5.13" // 07-11-2026 (up to SD_DRAIN queued blocks per card cycle)

  // hardware (descriptive only)
  //   ADC:  TLV320ADC6140 (ADC_V2 board), TDM, 32 bit
  //   RTC:  RV-3028 (Pimoroni breakout) on RP2040/RP2350; SNVS RTC (coin cell) on Teensy 4.1
  //   base: LC_MARE_09_2025 (RP); LC_Mare4 or cVAS3 carrier (Teensy, HW_BOARD in config.h)

  #define PREAMP    0     // 0: CMOS; 1: FET; 2: Mark (selects analog gain and ADC channel)

  #define WAIT      5     // seconds to wait for USB serial at boot (0: do not wait)

  //------------------------------------
  // MCU dispatch
  //------------------------------------
  #define T_4_1   0
  #define RP_2040 1
  #define RP_2350 2
  #if defined(__IMXRT1062__)
    #define MCU T_4_1
    #define MCU_NAME "T4.1"
  #elif defined(ARDUINO_ADAFRUIT_FEATHER_RP2040_ADALOGGER)
    #define MCU RP_2040
    #define MCU_NAME "RP2040"
  #elif defined(ARDUINO_ADAFRUIT_FEATHER_RP2350_HSTX)
    #define MCU RP_2350
    #define MCU_NAME "RP2350"
  #else
    #error "unsupported board: use Teensy 4.1, Adafruit Feather RP2040 Adalogger or Feather RP2350 HSTX"
  #endif

  #if (PROC_MODE != 0) && (PROC_MODE != 1)
    #error "PROC_MODE must be 0 (raw) or 1 (compressed)"
  #endif

  // epoch of the MCU clock rtc_get(): 1970 on the Teensy SNVS RTC (core function),
  // 2000 on the RP RTC. Only record headers are converted to unix seconds (rtc_get_unix).
  #if MCU==T_4_1
    #define RTC_EPOCH 1970
  #else
    #define RTC_EPOCH 2000
  #endif

  //------------------------------------
  // pins
  //------------------------------------
  #if MCU==T_4_1
    // carrier board selects the ADC control pins (HW_BOARD in config.h);
    // SD card in the built-in SDIO slot, no external RTC (SNVS RTC and alarm)
    #if HW_BOARD==HW_LC_MARE4
      #define ADC_EN      2       // ADC board LDO enable
      #define ADC_SHDNZ   3       // ADC shutdown (active low)
    #elif HW_BOARD==HW_CVAS3
      #define ADC_EN      32
      #define ADC_SHDNZ   33
      #define ADC_EN2     31      // second LDO (digital-side supply)
    #else
      #error "HW_BOARD must be HW_LC_MARE4 or HW_CVAS3 (see config.h)"
    #endif
    // TDM data lines = TLV320ADC6140 codecs: 1 (0x4C on RX_DATA0, pin 8); the acquisition
    // and ADC code also supports 2 (0x4D on RX_DATA1, pin 6; microPAM_V5 four-channel board)
    #define NPORT       1
  #else
    #define ADC_EN      5
    #define ADC_SHDNZ   6
    #define HP_ON       14      // headphone amplifier (not used, held low)
    #define NPORT       1
    #if MCU==RP_2040
      #define SD_CS       23
      #define XRTC_INT_PIN 15
    #elif MCU==RP_2350
      #define SD_CS       25
      #define XRTC_INT_PIN 29
    #endif
  #endif

  //------------------------------------
  // acquisition
  //------------------------------------
  // NCH: channels (TDM slots and WAV header)
  #if MCU==T_4_1
    // the SAI frame always has 4 slots (bit clock 128*fs, as in microPAM_V5);
    // unused slots are masked, so the rate limit is 192 kHz for 2 and for 4 channels
    #if NCHAN==0 && NPORT==1
      #define NCH       2     // ch2, ch3 of codec 0x4C, as on RP2350
    #elif NCHAN==4 || NPORT==2
      #define NCH       4     // NPORT 1: all four inputs; NPORT 2: ch1, ch2 of each codec
    #else
      #error "NCHAN must be 0 (board default) or 4"
    #endif
    #define FSAMP_MAX   192000  // bit clock 4*32*192 kHz = 24.576 MHz
  #elif NCHAN==0 && MCU==RP_2040
    #define NCH         1
    #define FSAMP_MAX   384000  // bit clock 32*384 kHz = 12.288 MHz; 32 kB queue blocks: the
                            // 160 kB queue holds only about 0.2 s at 384 kHz (compressed)
  #elif NCHAN==0
    #define NCH         2
    #define FSAMP_MAX   384000
  #elif NCHAN==4
    #define NCH         4
    #define FSAMP_MAX   192000  // bit clock 4*32*192 kHz = 24.576 MHz, as 2 channels at 384 kHz
  #else
    #error "NCHAN must be 0 (board default) or 4"
  #endif
  #if FSAMP>FSAMP_MAX
    #undef  FSAMP
    #define FSAMP FSAMP_MAX
  #endif

  #define MBIT      32      // bits per TDM slot

  #define NBUF_ACQ  1024    // words per DMA buffer (one acquisition chunk)
  #if MCU==T_4_1 || defined(RP2350_PSRAM_CS)
    #define MD      64      // acquisition chunks per queue block (disk write unit, 256 kB):
                            // fewer SD select/write/programming/deselect cycles (needs PSRAM)
  #else
    #define MD      8       // acquisition chunks per queue block (disk write unit, 32 kB)
  #endif
  #define NBLOCK    (MD*NBUF_ACQ)

  #if MCU==T_4_1
    #define MAX_QUEUE_PSRAM (32)  //  32*64*1024*4 B = 8 MB (EXTMEM), used if PSRAM is fitted
    #define MAX_QUEUE (1)   //   1*64*1024*4 B = 256 kB (DMAMEM), fallback without PSRAM:
                            // one block plus diskBuffer, not fit for deployment
  #elif defined(RP2350_PSRAM_CS)
    #define MAX_QUEUE (28)  //  28*64*1024*4 B = 7 MB (8 MB PSRAM)
  #else
    #define MAX_QUEUE (5)   //   5*8*1024*4 B = 160 kB
  #endif

  // file format (identical to microPAM_V5)
  #define COMP_MAGIC 0x434F4D50u  // "COMP": compressed record sync word
  #define NAVG       1            // no averaging (V5 IKEY field)

  // right shift applied before compression (drops the 8 zero-padding bits and 4 noise bits)
  #if PROC_MODE==0
    #define SHIFT (0)
  #else
    #define SHIFT (8+4)
  #endif

  // ADC gain
  #if PREAMP==0
    #define AGAIN 20        // dB
  #else
    #define AGAIN 0
  #endif
  #define DGAIN 0

  // MCU clock follows the sampling frequency (multiples of 12 MHz; RP only, Teensy keeps 600 MHz)
  #if NCH==4        // same word rates as 2 channels at twice the sampling frequency
    #if FSAMP<=96000 || MCU==RP_2350
      #define CLK_MULT 8    // 96 MHz (RP2350 also at 192 kHz: Cortex-M33, untested)
    #else
      #define CLK_MULT 12   // 144 MHz (RP2040 at 192 kHz)
    #endif
  #elif FSAMP<=96000
    #define CLK_MULT 4      // 48 MHz
  #else
    #define CLK_MULT 8      // 96 MHz up to 384 kHz. RP2040: 1 channel at 384 kHz has the word rate
                            // of 2 at 192 kHz; 192 MHz gave the same card stalls and about 15 mA
                            // more. RP2350 (was 144 MHz at 2 x 384 kHz): untested
  #endif

  #define SD_DRAIN  8       // queued blocks written at most per card cycle (wake, write, stand-by);
                            // 1: one block per cycle (before 3.5.13). After a slow write the
                            // backlog then goes to the card as one longer write command
  #define SD_MULT   6       // SPI clock 72 MHz (a little high, but works)
  #define SD_SDIO_MHZ 24    // RP2040 PIO SDIO clock (MHz), at most 25 (default speed; high speed
                            // fails with the PIO driver); needs CLK_MULT*12 >= 4*SD_SDIO_MHZ

  // RP-SDK attribute for RAM-resident functions; Teensy code runs from ITCM anyway
  #if MCU==T_4_1
    #define __not_in_flash_func(func) func
  #endif

  //------------------------------------
  // program states
  //------------------------------------
  enum status_t {DO_START, CLOSED, RECORDING, MUST_STOP, JUST_STOPPED, STOPPED};
  extern const char status_text[][16];

  //------------------------------------
  // run-time parameters
  //------------------------------------
  // filing.cxx
  extern uint16_t t_acq;   // seconds (each file)
  extern uint16_t t_on;    // minutes (each on period)
  extern uint16_t t_rep;   // minutes (continuous recording if t_rep <= t_on)
  extern uint16_t h_rec[]; // hours for selective recordings {0,12,12,24}

  extern char ISRC[];
  extern char ICMS[];
  extern char IART[];
  extern char IPRD[];
  extern char ISBJ[];
  extern char INAM[];
  extern char startTime[]; // yyyy-mm-dd_hh:mm:ss

  // rp2x.cxx
  extern uint32_t fsamp;   // Hz

  // adc.cxx
  extern uint32_t again;   // dB

  // .ino
  extern uint32_t loop_count;
  extern uint32_t data_count;

#endif
