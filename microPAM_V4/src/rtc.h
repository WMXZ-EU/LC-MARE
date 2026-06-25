/* microPAM 
 * Copyright (c) 2026, Walter Zimmer
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

  void XRTCgetDatetime(datetime_t *t);
  void XRTCsetDatetime(datetime_t *t);
  void XRTCsetAlarm(uint32_t secs);
  void printDatetime(const char *str, datetime_t *t);

  void encodeTimestamp(char *txt, datetime_t *tm);
  void decodeTimestamp(datetime_t*tm,  char *txt);
  
    void rtcSetDatetime(datetime_t *t);
    void rtcGetDatetime(datetime_t *t);

  extern char startTime[];
  void hibernate_until(uint32_t secs);
#endif