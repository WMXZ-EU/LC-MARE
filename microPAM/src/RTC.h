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
#ifndef RTC_H
#define RTC_H

#if defined(TARGET_RP2040)
#include "hardware/rtc.h"
  #define SDA 8
  #define SCL 9

  uint32_t rtc_get(void);

#elif defined(__IMXRT1062__)
  #define SDA 18
  #define SCL 19


  /** \struct datetime_t
  *  \ingroup util_datetime
  *  \brief Structure containing date and time information
  *
  *    When setting an RTC alarm, set a field to -1 tells
  *    the RTC to not match on this field
  */
  typedef struct {
      int16_t year;    ///< 0..4095
      int8_t month;    ///< 1..12, 1 is January
      int8_t day;      ///< 1..28,29,30,31 depending on month
      int8_t dotw;     ///< 0..6, 0 is Sunday
      int8_t hour;     ///< 0..23
      int8_t min;      ///< 0..59
      int8_t sec;      ///< 0..59
  } datetime_t;

  bool rtc_set_datetime(datetime_t *t);
  bool rtc_get_datetime(datetime_t *t);
  char * rtcGetTimestamp(void);

  void rtcSync(void);
  void rtcXferTime(void);
#endif

void rtcSetDate(int year,int month,int day);
void rtcSetTime(int hour,int minutes,int seconds);
int16_t rtcSetup(uint8_t sda=SDA, uint8_t scl=SCL);
void time2date(uint32_t time, datetime_t *tm);

#endif