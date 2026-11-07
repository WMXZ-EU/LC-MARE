/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
// MCU real-time clock and external RV-3028 RTC (time keeping and wake-up alarm).
// On RP2040/RP2350 all second counts are relative to 2000-01-01 00:00:00; on Teensy 4.1 the
// MCU clock is the battery-backed SNVS RTC, counting from 1970 (RTC_EPOCH in global.h), and
// the "external RTC" functions (XRTC...) act on it, including the wake-up alarm.
#ifndef RTC_H
#define RTC_H
#include <stdint.h>
#include "global.h"

#ifdef HAS_RP2040_RTC
  #include "hardware/rtc.h"
#else
  typedef struct {
      int16_t year;    ///< 0..4095
      int8_t month;    ///< 1..12, 1 is January
      int8_t day;      ///< 1..28,29,30,31 depending on month
      int8_t dotw;     ///< 0..6, 0 is Sunday
      int8_t hour;     ///< 0..23
      int8_t min;      ///< 0..59
      int8_t sec;      ///< 0..59
  } datetime_t;

  void rtc_init(void);
  bool rtc_running(void);
  bool rtc_get_datetime(datetime_t *t);
  bool rtc_set_datetime(const datetime_t *t);
#endif

  // external RTC on Wire (RP only)
  #define RTC_SDA   2
  #define RTC_SCL   3

  int16_t rtc_setup(void);    // start MCU RTC and sync it from the external RTC
  #if MCU!=T_4_1
    uint32_t rtc_get(void);   // MCU RTC, seconds since 2000 (Teensy: core function, since 1970)
  #endif
  uint32_t rtc_get_unix(void); // MCU RTC, seconds since 1970 (record headers)

  void time2date(uint32_t seconds, datetime_t *tm, uint16_t epoch);
  uint32_t date2time(const datetime_t *tm, uint16_t epoch);
  void printDatetime(const char *str, const datetime_t *t);

  void rtcGetDatetime(datetime_t *t);
  void rtcSetDatetime(datetime_t *t);

  void XRTCgetDatetime(datetime_t *t);
  void XRTCsetDatetime(const datetime_t *t);
  void XRTCsetAlarm(uint32_t secs);
  int16_t XRTCclearAlarm(void);

#endif
