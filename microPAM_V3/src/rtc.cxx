/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
#include "Arduino.h"
#include <Wire.h>
#include "global.h"
#include "i2c.h"
#include "rtc.h"

/*********************************** date conversion ***********************************/
#define D2000 10957     // days between 1970-01-01 and 2000-01-01
#define LEAP_YEAR(Y)  ((((Y) % 4) == 0) && ((((Y) % 100) != 0) || (((Y) % 400) == 0)))

static const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

void time2date(uint32_t seconds, datetime_t *tm, uint16_t epoch)
{
  uint32_t time = seconds;
  tm->sec  = time % 60;  time /= 60;
  tm->min  = time % 60;  time /= 60;
  tm->hour = time % 24;  time /= 24;

  // time counts now days since epoch; 1970-01-01 was a Thursday (4)
  uint32_t dotw = (epoch == 2000) ? (D2000+4) % 7 : 4;
  tm->dotw = (time + dotw) % 7;

  uint32_t year = epoch;
  uint32_t days = 0;
  while((days += (LEAP_YEAR(year) ? 366 : 365)) <= time) year++;
  tm->year = year;

  days -= LEAP_YEAR(year) ? 366 : 365;
  days = time - days;   // day in this year, starting at 0

  uint32_t month;
  for (month = 1; month < 12; month++)
  { uint16_t monthLength = daysInMonth[month-1];
    if ((month == 2) && LEAP_YEAR(year)) monthLength++;
    if (days < monthLength) break;
    days -= monthLength;
  }
  tm->day   = days + 1;
  tm->month = month;
}

uint32_t date2time(const datetime_t *tm, uint16_t epoch)
{
  uint16_t year = tm->year;
  uint32_t days = 0;
  for (int y = epoch; y < year; y++) days += (LEAP_YEAR(y) ? 366 : 365);
  for (int ii = 1; ii < tm->month; ii++)
  { days += daysInMonth[ii-1];
    if ((ii == 2) && LEAP_YEAR(year)) days++;
  }
  days += tm->day - 1;
  return days*86400 + tm->hour*3600 + tm->min*60 + tm->sec;
}

void printDatetime(const char *str, const datetime_t *t)
{
  Serial.printf("%s %04d-%02d-%02d %02d:%02d:%02d\n", str,
                t->year, t->month, t->day, t->hour, t->min, t->sec);
}

/*********************************** MCU RTC *******************************************/
#if MCU==T_4_1
  // SNVS RTC: rtc_get()/rtc_set() of the Teensy core, seconds since 1970
  void rtc_init(void) {}
  bool rtc_running(void) { return (SNVS_LPCR & 1); }

  bool rtc_get_datetime(datetime_t *t)
  { time2date(rtc_get(), t, RTC_EPOCH);
    return true;
  }

  bool rtc_set_datetime(const datetime_t *t)
  { rtc_set(date2time(t, RTC_EPOCH));
    return true;
  }
#elif !defined(HAS_RP2040_RTC)
  // RP2350: always-on timer replaces the RP2040 calendar RTC
  #include "pico/aon_timer.h"

  void rtc_init(void)
  { struct timespec ts = {0, 0};
    aon_timer_start(&ts);
  }

  bool rtc_running(void) { return aon_timer_is_running(); }

  bool rtc_get_datetime(datetime_t *t)
  { time2date(rtc_get(), t, 2000);
    return true;
  }

  bool rtc_set_datetime(const datetime_t *t)
  { struct timespec ts = {(time_t) date2time(t, 2000), 0};
    aon_timer_set_time(&ts);
    return true;
  }

  uint32_t rtc_get(void)
  { struct timespec ts;
    aon_timer_get_time(&ts);
    return ts.tv_sec;
  }
#else
  uint32_t rtc_get(void)
  { datetime_t tm;
    rtc_get_datetime(&tm);
    return date2time(&tm, 2000);
  }
#endif

#define T2000 946684800u  // seconds between 1970-01-01 and 2000-01-01
#if RTC_EPOCH==1970
  uint32_t rtc_get_unix(void) { return rtc_get(); }
#else
  uint32_t rtc_get_unix(void) { return rtc_get() + T2000; }
#endif

void rtcGetDatetime(datetime_t *t) { rtc_get_datetime(t); }

void rtcSetDatetime(datetime_t *t)
{ // RP2040 RTC rejects an out-of-range weekday, so derive it from the date
  datetime_t tmp;
  time2date(date2time(t, 2000), &tmp, 2000);
  t->dotw = tmp.dotw;
  rtc_set_datetime(t);
}

#if MCU==T_4_1
/*********************************** SNVS (Teensy 4.1) *********************************/
// no external RTC: the SNVS RTC keeps time on the coin cell and provides the wake-up alarm
#define SNVS_LPCR_LPTA_EN  (1 << 1)
#define SNVS_LPSR_LPTA     (1 << 0)

void XRTCgetDatetime(datetime_t *t)       { rtcGetDatetime(t); }
void XRTCsetDatetime(const datetime_t *t) { rtc_set_datetime(t); }

// clear a fired alarm and disable the alarm; returns the alarm flag found (1: an alarm had fired)
int16_t XRTCclearAlarm(void)
{ int16_t af = (SNVS_LPSR & SNVS_LPSR_LPTA) ? 1 : 0;
  SNVS_LPCR &= ~SNVS_LPCR_LPTA_EN;
  while (SNVS_LPCR & SNVS_LPCR_LPTA_EN);
  SNVS_LPSR = SNVS_LPSR_LPTA;                 // write 1 to clear
  return af;
}

// alarm at secs (seconds since 1970); switches the power on again after hibernate_until()
void XRTCsetAlarm(uint32_t secs)
{ datetime_t tm;
  time2date(secs, &tm, RTC_EPOCH);
  Serial.printf("alarm %04d-%02d-%02d %02d:%02d:%02d\n", tm.year, tm.month, tm.day, tm.hour, tm.min, tm.sec);

  uint32_t lpcr = SNVS_LPCR;
  SNVS_LPSR = SNVS_LPSR_LPTA;
  asm volatile("dsb");
  SNVS_LPCR &= ~SNVS_LPCR_LPTA_EN;            // alarm off while setting it
  while (SNVS_LPCR & SNVS_LPCR_LPTA_EN);
  __disable_irq();
  SNVS_LPTAR = secs;
  while (SNVS_LPTAR != secs);
  SNVS_LPCR = lpcr | SNVS_LPCR_LPTA_EN;
  while (!(SNVS_LPCR & SNVS_LPCR_LPTA_EN));
  __enable_irq();
}

int16_t rtc_setup(void)
{ // the SNVS RTC runs on the coin cell and is set by the Teensy loader at upload: do not reset it
  datetime_t t;
  rtcGetDatetime(&t);
  printDatetime("rtc ", &t);
  if(rtc_get() < 1735689600u) Serial.println("rtc not set (before 2025): set it with '!d'");
  return 0;   // no external RTC
}

#else
/*********************************** RV-3028 *******************************************/
// partially based on https://github.com/constiko/RV-3028_C7-Arduino_Library
#define RV3028_ADDR             0x52

#define RV3028_SECONDS          0x00
#define RV3028_MINUTES_ALM      0x07
#define RV3028_STATUS           0x0E
#define RV3028_CTRL1            0x0F
#define RV3028_CTRL2            0x10
#define RV3028_INT_MASK         0x12
#define RV3028_EEPROM_ADDR      0x25
#define RV3028_EEPROM_DATA      0x26
#define RV3028_EEPROM_CMD       0x27
#define EEPROM_Backup_Register  0x37

#define STATUS_AF               2
#define STATUS_EEBUSY           7
#define CTRL1_EERD              3
#define CTRL1_WADA              5
#define CTRL2_AIE               3
#define IMT_MASK_CAIE           2

#define EEPROMBackup_FEDE_BIT   4             // fast edge detection enable
#define EEPROMBackup_BSM_SHIFT  2             // backup switchover mode
#define EEPROMBackup_BSM_CLEAR  0b11110011

#define EEPROMCMD_First         0x00
#define EEPROMCMD_Update        0x11
#define EEPROMCMD_ReadSingle    0x22

static uint8_t bin2bcd(uint8_t val) { return val + 6 * (val / 10); }
static uint8_t bcd2bin(uint8_t val) { return val - 6 * (val >> 4); }

static uint16_t haveExtRTC = 0;

static i2c_class &xrtc(void)
{ static i2c_class i2c(&Wire);  // constructed (Wire.begin) on first use, after pins are set
  return i2c;
}

static uint8_t readRegister(uint8_t reg)            { return xrtc().read(RV3028_ADDR, reg); }
static void writeRegister(uint8_t reg, uint8_t val) { xrtc().write(RV3028_ADDR, reg, val); }

static void setBit(uint8_t reg, uint8_t bit)   { writeRegister(reg, readRegister(reg) |  (1 << bit)); }
static void clearBit(uint8_t reg, uint8_t bit) { writeRegister(reg, readRegister(reg) & ~(1 << bit)); }

// true if EEPROM became ready, false on timeout
static bool waitforEEPROM(void)
{ uint32_t to = millis();
  while ((readRegister(RV3028_STATUS) & (1 << STATUS_EEBUSY)) && (millis() - to < 500));
  return (millis() - to) < 500;
}

static bool writeConfigEEPROM_RAMmirror(uint8_t eepromaddr, uint8_t val)
{ bool success = waitforEEPROM();
  setBit(RV3028_CTRL1, CTRL1_EERD);           // disable auto refresh
  writeRegister(eepromaddr, val);             // configuration RAM
  writeRegister(RV3028_EEPROM_CMD, EEPROMCMD_First);
  writeRegister(RV3028_EEPROM_CMD, EEPROMCMD_Update);  // RAM -> EEPROM
  if (!waitforEEPROM()) success = false;
  clearBit(RV3028_CTRL1, CTRL1_EERD);         // re-enable auto refresh
  if (!waitforEEPROM()) success = false;
  return success;
}

static uint8_t readConfigEEPROM_RAMmirror(uint8_t eepromaddr)
{ bool success = waitforEEPROM();
  setBit(RV3028_CTRL1, CTRL1_EERD);
  writeRegister(RV3028_EEPROM_ADDR, eepromaddr);
  writeRegister(RV3028_EEPROM_CMD, EEPROMCMD_First);
  writeRegister(RV3028_EEPROM_CMD, EEPROMCMD_ReadSingle);
  if (!waitforEEPROM()) success = false;
  uint8_t eepromdata = readRegister(RV3028_EEPROM_DATA);
  if (!waitforEEPROM()) success = false;
  clearBit(RV3028_CTRL1, CTRL1_EERD);
  return success ? eepromdata : 0xFF;
}

// 0: disabled; 1: direct switching; 2: standby; 3: level switching (factory default)
static bool setBackupSwitchoverMode(uint8_t val)
{ if (val > 3) return false;
  uint8_t backup = readConfigEEPROM_RAMmirror(EEPROM_Backup_Register);
  if (backup == 0xFF) return false;
  backup |= 1 << EEPROMBackup_FEDE_BIT;
  backup &= EEPROMBackup_BSM_CLEAR;
  backup |= val << EEPROMBackup_BSM_SHIFT;
  return writeConfigEEPROM_RAMmirror(EEPROM_Backup_Register, backup);
}

static int16_t initXRTC(uint8_t sda, uint8_t scl)
{ Wire.setSDA(sda);
  Wire.setSCL(scl);
  Serial.print("check XRTC: 0x"); Serial.println(RV3028_ADDR, HEX);
  if(!xrtc().exist(RV3028_ADDR))
  { Serial.println("XRTC does not exist");
    return 0;
  }
  Serial.print("Config EEPROM 0x37 before: "); Serial.println(readConfigEEPROM_RAMmirror(EEPROM_Backup_Register));
  setBackupSwitchoverMode(1);
  Serial.print("Config EEPROM 0x37 after:  "); Serial.println(readConfigEEPROM_RAMmirror(EEPROM_Backup_Register));
  return 1;
}

void XRTCgetDatetime(datetime_t *t)
{ uint8_t buf[7] = {0};   // sec, min, hour, weekday, date, month, year
  if(!haveExtRTC) return;
  xrtc().readData(RV3028_ADDR, RV3028_SECONDS, buf, 7);
  for(int ii = 0; ii < 7; ii++) buf[ii] = bcd2bin(buf[ii]);
  t->year  = buf[6] + 2000;
  t->month = buf[5] & 0x7f;
  t->day   = buf[4];
  t->hour  = buf[2];
  t->min   = buf[1];
  t->sec   = buf[0];
  t->dotw  = 0;           // not used; rtcSetDatetime derives it
}

void XRTCsetDatetime(const datetime_t *t)
{ uint8_t buf[7] = {0};
  if(!haveExtRTC) return;
  buf[6] = t->year - 2000;
  buf[5] = t->month;
  buf[4] = t->day;
  buf[2] = t->hour;
  buf[1] = t->min;
  buf[0] = t->sec;
  for(int ii = 0; ii < 7; ii++) buf[ii] = bin2bcd(buf[ii]);
  xrtc().writeData(RV3028_ADDR, RV3028_SECONDS, buf, 7);
}

// clear a pending alarm and disable the alarm interrupt, which releases the INT pin;
// returns the alarm flag found (1: an alarm had fired)
// Without this, a flag left from an earlier wake-up keeps INT low, and every later boot
// is taken for an alarm wake-up (no wait for USB serial).
int16_t XRTCclearAlarm(void)
{ if(!haveExtRTC) return 0;
  int16_t af = (readRegister(RV3028_STATUS) >> STATUS_AF) & 1;
  clearBit(RV3028_CTRL2, CTRL2_AIE);
  clearBit(RV3028_STATUS, STATUS_AF);
  return af;
}

// alarm on matching hour and minute (once per day); INT pin goes low and wakes the MCU
void XRTCsetAlarm(uint32_t secs)
{ datetime_t tm;
  time2date(secs, &tm, 2000);
  Serial.printf("alarm %04d-%02d-%02d %02d:%02d\n", tm.year, tm.month, tm.day, tm.hour, tm.min);

  clearBit(RV3028_CTRL2, CTRL2_AIE);          // no interrupt while configuring
  clearBit(RV3028_STATUS, STATUS_AF);
  setBit(RV3028_CTRL1, CTRL1_WADA);           // date (not weekday) alarm

  uint8_t alarmTime[3];
  alarmTime[0] = bin2bcd(tm.min);
  alarmTime[1] = bin2bcd(tm.hour);
  alarmTime[2] = bin2bcd(tm.day) | (1 << 7);  // date disabled: hour and minute match
  xrtc().writeData(RV3028_ADDR, RV3028_MINUTES_ALM, alarmTime, 3);

  setBit(RV3028_CTRL2, CTRL2_AIE);
  clearBit(RV3028_INT_MASK, IMT_MASK_CAIE);   // no clock output on alarm
}

/*********************************** setup *********************************************/
int16_t rtc_setup(void)
{
  // start MCU RTC at a default time, so it runs even without external RTC
  datetime_t t = {2026, 1, 1, 4, 0, 0, 0};
  rtc_init();
  rtcSetDatetime(&t);

  haveExtRTC = initXRTC(RTC_SDA, RTC_SCL);
  Serial.print("extClock "); Serial.println(haveExtRTC);

  if(haveExtRTC)
  { XRTCgetDatetime(&t);
    printDatetime("XRTC", &t);
    rtcSetDatetime(&t);
    delay(10);
  }
  rtcGetDatetime(&t);
  printDatetime("rtc ", &t);
  return haveExtRTC;
}

#endif
