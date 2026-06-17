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

#endif