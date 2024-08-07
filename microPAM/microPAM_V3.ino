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
 
 /*
  microPAM purpose:
  logging of acoustic data 
  use of Teensy 4.1 or RP2040
  use if I2S Microphone or TLV320ADC6140 
  working system T4.1 + TLV320ADC6140 
 */
 
#include "Arduino.h"
#if defined(__IMXRT1062__)
  #include <CrashReport.h>
#endif

#if defined(TARGET_RP2040)
  #include "pico/stdlib.h"
#endif

#include "src/Config.h"
#include "src/Queue.h"
#include "src/Acq.h"
#include "src/RTC.h"
#include "src/Compress.h"
#include "src/Filing.h"
#include "src/Menu.h"

#include "src/Adc.h"

#if defined(__IMXRT1062__) && defined(AUDIO_INTERFACE)
  #include <AudioStream.h>
  #include <usb_audio.h>
  #include "src/AudioTrigger.h"
  #include "src/AudioIF.h"

  AudioTrigger    trigger;
  AudioIF         acqIF(FSAMP);
  AudioOutputUSB  usb; 
  AudioConnection patchCord1(acqIF, 0, usb, 0);
  AudioConnection patchCord2(acqIF, 1, usb, 1);
#endif

#if defined(__IMXRT1062__)
  extern "C" uint32_t set_arm_clock(uint32_t frequency); // clockspeed.c
#endif

#if defined(__IMXRT1062__)
  #include "src/MTP.h"

  MTPStorage_SD storage;
  MTPD mtpd(&storage);

  extern "C" void usb_init(void);
  void resetUSB(void)
  {
    USB1_USBCMD = 0;  // disconnect USB
    delay(100);       // long enough for PC+hubs to detect
    usb_init();
    while(!Serial) mtpd.loop();
  }

  void reboot(void) { *(uint32_t *)0xE000ED0C =  0x5FA0004;}

  #if USE_EVENTS==1
    extern "C" int usb_init_events(void);
    void resetMTP(void) {mtpd.send_DeviceResetEvent();}
  #else
    int usb_init_events(void) {return 0;}
    void resetMTP(void) {}

  #endif

  uint32_t getTeensySerial(void) { return (HW_OCOTP_MAC0 & 0xFFFFFF); }

  void storage_configure();  

  void lowPowerInit(void)
  {
    // keep memory powered during sleep
    CCM_CGPR |= CCM_CGPR_INT_MEM_CLK_LPM;
    // keep cpu clock on in wait mode (required for systick to trigger wake-up)
    CCM_CLPCR &= ~(CCM_CLPCR_ARM_CLK_DIS_ON_LPM | CCM_CLPCR_LPM(3));
    // set SoC low power mode to wait mode
    CCM_CLPCR |= CCM_CLPCR_LPM(1);
    // ensure above config is done before executing WFI
    asm volatile("dsb");    
  }

#else
  void resetUSB(void) {}
  void reboot(void) {}
  void storage_configure() {}

  void storage_configure() {} 

  void lowPowerInit(void) {}
#endif

/***************************************************************************/
volatile int setup_ready=0;
volatile int termon=0;

  void checkInput(void)
  {
    while(!Serial.available()) ;
    char ch;
    ch=Serial.read();
    (void) ch;
  }

void setup1();  // contains acquisition module and is seperated core in RP2040
//
void setup() 
{
  // put your setup code here, to run once:
  #if defined(__IMXRT1062__)
    set_arm_clock(24'000'000);
  #elif defined(TARGET_RP2040)
    set_sys_clock_khz(48'000, true);
  #endif

  // only in case it is needed set baud rate (is ignored for USB-Serial)
  Serial.begin(115200);

  // check start or restart
  uint16_t *params=loadParameters();
  if(params[0]==1)
  {
    termon = (t_rep>t_on)? 0: 1;
  }
  else  // first time wait for terminal
  {
    // wait for 10 s to allow USB-Serial connection
    while(millis()<10'000) if(Serial) { termon=1; break;}
  }

  Serial.println(version);
  Serial.print("params[0] = "); Serial.println(params[0]);
  
  // Teensy has a crash report
  #if defined(__IMXRT1062__)
    if(CrashReport) Serial.print(CrashReport);
    #if defined(AUDIO_INTERFACE)
      AudioMemory(8);
    #endif
  #endif

  // Teensy has USB
  #if defined(__IMXRT1062__)
    #if USE_EVENTS==1
      usb_init_events();
    #endif
  #endif

  #if defined(__IMXRT1062__)
    usbPowerSetup();
    lowPowerInit();

    // configure disk storage
    storage_configure();
  #endif

  // setup RT Clock
  #if USE_EXT_RTC==1
    Serial.println("rtcSetup");
    rtcSetup();

    #if defined(__IMXRT1062__)
      rtcSync();
    #endif
  #endif

  datetime_t t;
  if(!rtc_get_datetime(&t)) Serial.println("failing get_datetime");
  Serial.printf("RTC-main: %4d-%02d-%02d %02d:%02d:%02d",
                           t.year,t.month,t.day,t.hour,t.min,t.sec); 
  Serial.println();
  //

  #if USE_EXT_RTC==1
    Serial.print("RV3028: ");
    Serial.println(rtcGetTimestamp());
  #endif
  //
  Serial.println("filing_init");
  filing_init();

  setup_ready=1;
  Serial.println("Setup done");

  // in case of single core teensy 4.1 start acquisition, which for rp2040 is in 2nd core
  #if defined(__IMXRT1062__)
    setup1();
    pinMode(13,OUTPUT);
  #endif
}

void loop() 
{
  // put your main code here, to run repeatedly:
  static uint32_t loopCount=0;
  loopCount++;
  
  // obtain some statistics on Queue usage
  static uint16_t mxb=0;
  uint16_t nb;
  nb = getDataCount();
  if(nb>mxb) mxb=nb;

  // set inititial status STOPPED if terminal was attached within 10 s, CLOSED otherwise 
  static volatile int16_t status=(termon==1)? STOPPED: CLOSED;
  status=menu(status);

  #if defined(__IMXRT1062__)
    if(status<0)
    { mtpd.loop();
    }

    // 
    {
      if(status>0) digitalWriteFast(13,HIGH);    
      // save data (filing will be handled inside saveData)
      status=saveData(status);  
      if(status>0) digitalWriteFast(13,LOW);
    }
  #else
    status=saveData(status);  
  #endif

  // once a second provide some information to User
  static uint32_t t0=0;
  uint32_t t1;
  if((t1=millis())>(t0+1000))
  { 
    if(monitor)
    {
      datetime_t t;
      rtc_get_datetime(&t);
      Serial.printf("\n%4d-%02d-%02d %02d:%02d:%02d %d",
                    t.year,t.month,t.day,t.hour,t.min,t.sec,t.dotw); Serial.print(" : ");

      Serial.print(loopCount);  Serial.print(" ");
      Serial.print(procCount);  Serial.print(" ");
      Serial.print(procMiss);   Serial.print(" ");
      Serial.printf("%3d",mxb); Serial.print("  ");
      Serial.print(disk_count); Serial.print("  ; ");

      if(proc==0)
      {
        for(int ii=0; ii<8;ii++){ Serial.printf("%9d ",logBuffer[ii]);}        
      }
      else
      {
        for(int ii=0; ii<MB;ii++){ Serial.printf("%2d ",proc_stat[ii]);}
        Serial.printf("%2d",max_stat);

        for(int ii=0; ii<MB;ii++){ proc_stat[ii]=0;}
        max_stat=0;        
      }
    }

    loopCount=0;
    procCount=0;
    procMiss=0;
    mxb=0;
    disk_count=0;

    t0=t1;
  }
  asm("wfi");
}
/******************************************************************************e****/
// rp2040 has two core. let acquisition run on its own core
// on teensy this will be called from setup()

void setup1()
{ 
  Serial.println("Setup1");
  while(!setup_ready) {delay(1);} // wait for setup() to finish
  i2s_setup();
  dma_setup();
  adc_init();

  Serial.println("Setup1 done");
}

void loop1(){}  // nothing to be done here

/************************************Device specific functions *****************************/
#if defined(__IMXRT1062__)
  #ifndef BUILTIN_SDCARD
    #define BUILTIN_SDCARD 254
  #endif

  // needed for MTP
  const char *sd_str[]={"sdio"};          // edit to reflect your configuration
  const int cs[] = {BUILTIN_SDCARD};      // edit to reflect your configuration
  const int nsd = sizeof(sd_str)/sizeof(const char *);

  SdFs sdx[nsd];

  #define SD_CONFIG SdioConfig(FIFO_SDIO)

  void storage_configure()
  {
    // Using SD card for storage
      for(int ii=0; ii<nsd; ii++)
      { 
        /* for spi cards need chip select pin */
        if(cs[ii]<BUILTIN_SDCARD)
        { pinMode(cs[ii],OUTPUT); digitalWrite(cs[ii],HIGH); 
        }
        
        if(!sdx[ii].begin(SdioConfig(FIFO_SDIO))) 
        { Serial.printf("SD/SDIO Storage %d %d %s failed or missing",ii,cs[ii],sd_str[ii]);  Serial.println();
        }
        else
        {
          storage.addFilesystem(sdx[ii], sd_str[ii]);
          uint64_t totalSize = sdx[ii].clusterCount();
          uint64_t freeSize  = sdx[ii].freeClusterCount();
          uint32_t clusterSize = sdx[ii].bytesPerCluster();
          Serial.printf("SDIO Storage %d %d %s ",ii,cs[ii],sd_str[ii]); 
            Serial.print("; total clusters: "); Serial.print(totalSize); 
            Serial.print(" free clusters: "); Serial.print(freeSize);
            Serial.print(" clustersize: "); Serial.print(clusterSize/1024); Serial.println(" kByte");
        }
      }
  }
#endif
