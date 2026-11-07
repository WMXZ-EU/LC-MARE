/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
// Platform API: TDM acquisition, sleep, UID, NeoPixel.
// Implemented in rp2x.cxx (RP2040/RP2350: PIO + DMA, dormant sleep) and in
// teensy.cxx (Teensy 4.1: SAI1 + DMA, power-off with SNVS alarm).
#ifndef RP2X_H
#define RP2X_H
#include <stdint.h>
#include "rtc.h"

  // acquisition
  void i2s_setup(void);
  void dma_setup(void);
  void acqModifyFrequency(uint32_t fsamp);

  extern volatile uint32_t missed_acq;
  extern volatile uint32_t acq_count;

  // power
  void doReboot(void);
  void hibernate_until(uint32_t secs);   // sleep until secs (rtc_get() scale); restarts the program

  void encodeTimestamp(char *txt, const datetime_t *tm);
  void decodeTimestamp(datetime_t *tm, const char *txt);

  // board id
  extern char uid_strng[];
  void getUID(void);

  // NeoPixel
  void neo_pixel_init(void);
  void neo_pixel_show(uint16_t r, uint16_t g, uint16_t b);

#endif
