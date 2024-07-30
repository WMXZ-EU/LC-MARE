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
#include "Arduino.h"

#include "Wire.h"

static int16_t i2c_setup(uint8_t sda, uint8_t scl)
{
  Wire.setSDA(sda);
  Wire.setSCL(scl);
  Wire.begin();
  delay(100);
  return 1;
}

static int16_t i2c_exists(uint8_t address)
{
   Wire.beginTransmission(address);
   return !Wire.endTransmission();
}

static void i2c_write_register(uint8_t address, uint8_t reg, uint8_t val) 
{
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static uint8_t i2c_read_register(uint8_t address, uint8_t reg) 
{
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.endTransmission();

//  uint8_t buffer[1];
  Wire.requestFrom(address, (uint8_t)1);
  return Wire.read();
}

static void i2c_write_data(uint8_t address, uint8_t reg, uint8_t *buffer, uint16_t nbuf)
{
  Wire.beginTransmission(address);
  Wire.write(reg);
  for(int ii=0; ii<nbuf; ii++) Wire.write(buffer[ii]);
  Wire.endTransmission();  
}

static void i2c_read_data(uint8_t address, uint8_t reg, uint8_t *buffer, uint16_t nbuf)
{
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.endTransmission();

  Wire.requestFrom(address, (uint8_t)nbuf);
  for(int ii=0;ii<nbuf;ii++) buffer[ii]=Wire.read();
}

/**************************************************************************************/
#define DS3231_ADDRESS  0x68   ///< I2C address for DS3231
#define DS3231_TIME     0x00      ///< Time register
#define DS3231_ALARM1   0x07    ///< Alarm 1 register
#define DS3231_ALARM2   0x0B    ///< Alarm 2 register
#define DS3231_CONTROL  0x0E   ///< Control register
#define DS3231_STATUSREG 0x0F ///< Status register
#define DS3231_TEMPERATUREREG 0x11 

static uint8_t address = DS3231_ADDRESS;

static uint8_t bin2bcd(uint8_t val) { return val + 6 * (val / 10); }
static uint8_t bcd2bin(uint8_t val) { return val - 6 * (val >> 4); }

int16_t initRTC(uint8_t sda, uint8_t scl)
{
  i2c_setup(sda,scl);
  return i2c_exists(address);
}

int16_t lostPowerRTC(void) 
{
  return i2c_read_register(address,DS3231_STATUSREG) >> 7;
}

uint8_t *mgetRTC(uint8_t *buffer,uint16_t nbuf)
{
  i2c_read_data(address,DS3231_TIME,buffer,nbuf);
  for(int ii=0;ii<nbuf;ii++) buffer[ii]=bcd2bin(buffer[ii]);
  
  return buffer;
}

uint8_t *msetRTC(uint8_t *buffer, uint16_t nbuf)
{
  for(int ii=0;ii<nbuf;ii++) buffer[ii]=bin2bcd(buffer[ii]);
  i2c_write_data(address,DS3231_TIME,buffer,nbuf);

  uint8_t statreg = i2c_read_register(address,DS3231_STATUSREG);
  statreg &= ~0x80; // flip OSF bit
  i2c_write_register(address, DS3231_STATUSREG, statreg);

  return buffer;
}

/******************************************************************/
#include "Config.h"
#include "RTC.h"

/******************************************************************/
#if defined(TARGET_RP2040)

  const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint16_t date2days(uint16_t y, uint8_t m, uint8_t d) 
  {
    if (y >= 2000U) y -= 2000U;
    uint16_t days = d;
    for (uint8_t i = 1; i < m; ++i) days += daysInMonth[i - 1];
    if (m > 2 && y % 4 == 0) ++days;
    return days + 365 * y + (y + 3) / 4 - 1;
  }

  uint32_t time2seconds(uint16_t days, uint8_t h, uint8_t m, uint8_t s) 
  {
    return ((days * 24UL + h) * 60 + m) * 60 + s;
  }

  static uint8_t conv2d(const char *p) {
    uint8_t v = 0;
    if ('0' <= *p && *p <= '9')  v = *p - '0';
    return 10 * v + *++p - '0';
  }

  void adjustRTC(char *date, char *time)
  {
    //Mar 20 2023 17:34:05
    uint8_t rtcBuffer[7];
    
    char buff[11];
    int m;
    memcpy(buff, date, 11);
    // Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec
    switch (buff[0]) {
    case 'J':
      m = (buff[1] == 'a') ? 1 : ((buff[2] == 'n') ? 6 : 7);
      break;
    case 'F':
      m = 2;
      break;
    case 'A':
      m = buff[2] == 'r' ? 4 : 8;
      break;
    case 'M':
      m = buff[2] == 'r' ? 3 : 5;
      break;
    case 'S':
      m = 9;
      break;
    case 'O':
      m = 10;
      break;
    case 'N':
      m = 11;
      break;
    case 'D':
      m = 12;
      break;
    }
    rtcBuffer[6] = conv2d(buff + 9);
    rtcBuffer[5] = m;
    rtcBuffer[4] = conv2d(buff + 4);
    //
    memcpy(buff, time, 8);
    rtcBuffer[2] = conv2d(buff);
    rtcBuffer[1] = conv2d(buff + 3);
    rtcBuffer[0] = conv2d(buff + 6);

    rtcBuffer[3] = 0;

    msetRTC(rtcBuffer, 7);
  }

  int16_t rtcSetup(uint8_t sda, uint8_t scl)
  {
    static datetime_t t;
    static uint8_t rtcBuffer[7] = {0,27,15,1,3,4,23}; // adapt to better time (secs,min, ...., year-2000)

    rtc_init(); // hardware rtc
    initRTC(sda, scl); // external rtc clock
    delay(10);

// to set external rtc clock
#if 0
    msetRTC(rtcBuffer,7);
#endif

    mgetRTC(rtcBuffer,7);
    t.year=rtcBuffer[6]+2000;
    t.month=rtcBuffer[5]&0x7f;
    t.day=rtcBuffer[4];

    t.hour=rtcBuffer[2];
    t.min=rtcBuffer[1];
    t.sec=rtcBuffer[0] &0xff;

    Serial.printf("RTC init: %4d-%02d-%02d %02d:%02d:%02d",
                      t.year,t.month,t.day,t.hour,t.min,t.sec); Serial.println();

    while(!rtc_set_datetime(&t));
    delay(10); // give some time to settle

    return 1;
  }

  uint32_t rtc_get(void)
  {
    datetime_t t;
    rtc_get_datetime(&t);    
    uint16_t day;
    day=date2days(t.year, t.month, t.day);
    return time2seconds(day,t.hour, t.min, t.sec);
  }

  void syncRTC(datetime_t *t, uint8_t *rtcBuffer)
  {
      mgetRTC(rtcBuffer,7);
      t->year=rtcBuffer[6]+2000;
      t->month=rtcBuffer[5]&0x7f;
      t->day=rtcBuffer[4];

      t->hour=rtcBuffer[2];
      t->min=rtcBuffer[1];
      t->sec=rtcBuffer[0] &0xff;

      rtc_set_datetime(t);
  }

  void rtcSetDate(int year,int month,int day)
  {
    uint8_t rtcBuffer[7] = {0};//{0,27,15,1,3,4,23}; // adapt to better time (secs,min, ...., year)
    datetime_t t;
    mgetRTC(rtcBuffer,7);
    rtcBuffer[6]=year-2000;
    rtcBuffer[5]=month;
    rtcBuffer[4]=day;
    msetRTC(rtcBuffer,7);
    syncRTC(&t, rtcBuffer);
  }

  void rtcSetTime(int hour,int minutes,int seconds)
  {
    uint8_t rtcBuffer[7] = {0};//{0,27,15,1,3,4,23}; // adapt to better time (secs,min, ...., year)
    datetime_t t;
    mgetRTC(rtcBuffer,7);
    rtcBuffer[2]=hour;
    rtcBuffer[1]=minutes;
    rtcBuffer[0]=seconds;
    msetRTC(rtcBuffer,7);
    syncRTC(&t, rtcBuffer);
  }


#elif defined(__IMXRT1062__)

  #define YEAR0 1970
  #define LEAP_YEAR(Y)  ( ((YEAR0+(Y))>0) && !((YEAR0+(Y))%4) && ( ((YEAR0+(Y))%100) || !((YEAR0+(Y))%400) ) )

  //convenience macros to convert to and from tm years 
  #define  tmYearToCalendar(Y) ((Y) + YEAR0)  // full four digit year 
  #define  CalendarYrToTm(Y)   ((Y) - YEAR0)

  #define SECS_PER_MIN  ((uint32_t)(60UL))
  #define SECS_PER_HOUR ((uint32_t)(3600UL))
  #define SECS_PER_DAY  ((uint32_t)(SECS_PER_HOUR * 24UL))

  static  const uint8_t monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31}; // API starts months from 1, this array starts from 0
 
  void time2date(uint32_t time, datetime_t *tm)
  {
  // break the given time_t into time components
  // this is a more compact version of the C library localtime function
  // note that year is offset from YEAR0 !!!

    uint8_t year;
    uint8_t month, monthLength;
    unsigned long days;

    tm->sec  = time % 60; time /= 60; // now time is minutes
    tm->min  = time % 60; time /= 60; // now time is hours
    tm->hour = time % 24; time /= 24; // now time is days

    tm->dotw = ((time + 4) % 7) ;  // Sunday is day 0 // 1-1-1970 was Thursday
    
    year = 0;  
    days = 0;
    while((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) {
      year++;
    }
    tm->year = year+YEAR0;
    
    days -= LEAP_YEAR(year) ? 366 : 365;
    time -= days; // now time is days in this year, starting at 0
    
    days=0;
    month=0;
    monthLength=0;
    for (month=0; month<12; month++) {
      if (month==1) { // february
        if (LEAP_YEAR(year)) {
          monthLength=29;
        } else {
          monthLength=28;
        }
      } else {
        monthLength = monthDays[month];
      }
      
      if (time >= monthLength) {
        time -= monthLength;
      } else {
          break;
      }
    }
    tm->month = month + 1;  // jan is month 1  
    tm->day = time + 1;     // day of month
  }

  uint32_t date2time(datetime_t *tm)
  {
    int ii;
    uint32_t seconds;

    uint8_t year;
    year=tm->year-YEAR0; // year after 1-jan-1970 (YEAR0)
    #if 0
    seconds= year*(SECS_PER_DAY * 365);
    for (ii = 0; ii < year; ii++) {
      if (LEAP_YEAR(ii)) {
        seconds += SECS_PER_DAY;   // add extra days for leap years
      }
    }
    #endif
    uint32_t days= year*365;
    for (ii = 0; ii < year; ii++) if (LEAP_YEAR(ii)) days++;  // add extra days for leap years
    seconds = days*SECS_PER_DAY;

    // add days for this year, months start from 1
    for (ii = 1; ii < tm->month; ii++) {
      if ( (ii == 2) && LEAP_YEAR(year)) { 
        seconds += SECS_PER_DAY * 29;
      } else {
        seconds += SECS_PER_DAY * monthDays[ii-1];  //monthDay array starts from 0
      }
    }
    seconds+= (tm->day-1) * SECS_PER_DAY;
    seconds+= tm->hour * SECS_PER_HOUR;
    seconds+= tm->min * SECS_PER_MIN;
    seconds+= tm->sec;
    return seconds; 
  }

  #if USE_EXT_RTC==1
  #include "RV-3028-C7.h"
  RV3028 rtc;
  // needed functions
  // rtc.begin()
  // rtc.setUNIX(...)
  // rtc.setTime(...)
  // rtc.getUNIX()
  // rtc.updateTime()
  // rtc.stringTimeStamp()
  #endif

  int16_t rtcSetup(uint8_t sda, uint8_t scl)
  {
  #if USE_EXT_RTC==1
    Wire.begin();
    if (rtc.begin() == false) {
    Serial.println("RTC offline!");
    }
    else
    {
      Serial.println("RTC online!");
    }
    #endif
    
    return 1;
  }


  void rtcXferTime(void)
  {
  #if USE_EXT_RTC==1
    rtc.setUNIX(rtc_get());
    datetime_t t;
    rtc_get_datetime(&t);
    if (rtc.setTime(t.sec, t.min, t.hour, t.day, t.day, t.month, t.year) == false) 
    {
          Serial.println("Something went wrong setting the time");
    }
    #endif
  }
  void rtcSync(void)
  {
  #if USE_EXT_RTC==1
    uint32_t to;
    to=rtc_get();
    if (to<rtc.getUNIX()) rtc_set(rtc.getUNIX());
  #endif
  }

  char * rtcGetTimestamp(void)
  { 

  #if USE_EXT_RTC==1
    //PRINT TIME
    if (rtc.updateTime() == false) //Updates the time variables from RTC
    {
      Serial.println("RTC failed to update");
      return "";
    } else {
      return rtc.stringTimeStamp();
    }
    #else
      return 0;
    #endif
  }

  bool rtc_get_datetime(datetime_t *t)
  {
    time2date(rtc_get(), t);
    return 1;
  }

  bool rtc_set_datetime(datetime_t *t)
  {
    rtc_set(date2time(t));
    return 1;
  }

  void rtcSetDate(int year,int month,int day)
  { datetime_t t;
    rtc_get_datetime(&t);
    t.year=year;
    t.month=month;
    t.day=day;
    rtc_set_datetime(&t);
  }

  void rtcSetTime(int hour,int minutes,int seconds)
  { datetime_t t;
    rtc_get_datetime(&t);
    t.hour=hour;
    t.min=minutes;
    t.sec=seconds;
    rtc_set_datetime(&t);
  }

#endif
    
