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
 
#ifndef RP2X_H
#define RP2X_H

  void acqInit(uint32_t fs) ;
  void acqStart(void) ;
  void acqStop(void) ;
  void dma_exit(void);

  void acqModifyFrequency(uint32_t fsamp);
  
  void setAlarm(uint32_t secs);
  void goDormant(void) ;
   void doReboot(void);

  uint32_t getPSRAMSize(void);
  void set_MCU_clock(int32_t mcu_factor);
  void printCrashReport(void);

  extern char uid_strng[];
  void getUID(void);

  void stopSystem(void);
  void lowPowerInit(void);
  void usbPowerSetup(void);
  void usbPowerOff(void);
  void usbPowerExit(void);

  #ifndef HAS_RP2040_RTC
    typedef struct {
        int16_t year;    ///< 0..4095
        int8_t month;    ///< 1..12, 1 is January
        int8_t day;      ///< 1..28,29,30,31 depending on month
        int8_t dotw;     ///< 0..6, 0 is Sunday
        int8_t hour;     ///< 0..23
        int8_t min;      ///< 0..59
        int8_t sec;      ///< 0..59
    } datetime_t;

    void rtc_init(void) ;
    bool rtc_running(void) ;
    bool rtc_get_datetime(datetime_t *t) ;
    bool rtc_set_datetime(const datetime_t *t) ;
  #else
    #include "hardware/rtc.h"
  #endif

    uint32_t rtc_get(void) ;
    void rtc_set(uint32_t tt) ;

 void neo_pixel_show(uint16_t r, uint16_t g, uint16_t b);
#endif