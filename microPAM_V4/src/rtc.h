#ifndef RTC_H
#define RTC_H

#if MCU==T_4_1
  #include "Teensy.h"
#else
  #include "rp2x.h"
#endif


  int16_t rtc_setup(void );
  void time2date(uint32_t seconds, datetime_t *tm, uint16_t epoch);
  uint32_t date2time(const datetime_t *tm, uint16_t epoch);

  void XRTCsetAlarm(uint32_t secs);
  void encodeTimestamp(char *txt, datetime_t *tm);
  void decodeTimestamp(datetime_t*tm,  char *txt);
  
  extern char startTime[];
  void hibernate_until(uint32_t secs);
#endif