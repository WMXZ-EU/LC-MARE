
//
#include <Arduino.h>
#include "src/global.h"
#if MCU==T_4_1
  #include "src/Teensy.h"
#else
  #include "src/rp2x.h"
#endif

#include "src/acq.h"
#include "src/rtc.h"
#include "src/process.h"

/********************************* basics *******************************************************/
//  enum status_t  {DO_START, CLOSED, RECORDING, MUST_STOP, JUST_STOPPED, STOPPED, MUST_HIBERNATE};
char status_text[7][16]=
  {"DO_START\0", "CLOSED\0", "RECORDING\0", "MUST_STOP\0", "JUST_STOPPED\0", "STOPPED\0", "MUST_HIBERNATE\0"};

uint32_t fsamp=FSAMP;

uint32_t outdata[NBLOCK];

/********************************* main *********************************************************/
void setup() {
  // put your setup code here, to run once:

  while(millis()<5000) if(Serial) break;
  if(Serial)
  { Serial.println("\n*** micoPAM ***");
    Serial.print("millis: "); Serial.println(millis());
  }
  
  getUID();
  Serial.print("uid ");Serial.println(uid_strng);

  lowPowerInit();

  rtc_setup();
  //
  queue.reset();
  //
  acqInit(fsamp);
  acqStart();
}

void loop() {
  // put your main code here, to run repeatedly:
  static uint32_t cnt;
  if(queue.pull(outdata))
  {
    static uint32_t to=0;
    if(millis()>to+1000)
    { to=millis();
      Serial.printf("%d %d %d %d (%.1f%%): ",cnt++, acq_count, acq_missed,proc_time, proc_time/10.0*fsamp/1000.0/NBUF);
      acq_count=0;
      acq_missed=0;
      proc_time=0;
      for(int ii=0;ii<4;ii++) Serial.printf("%8x ",outdata[ii]); 
      for(int ii=4;ii<10;ii++) Serial.printf("%08x ",outdata[ii]); Serial.println();
    }
  }
  // testimg hibernate
  #if 0
    if(millis()>30000)
    { uint32_t tt=rtc_get();
      hibernate_until((tt/60+1)*60);
    }
  #endif
  asm("wfi");
}

#if 1
void setup1(void)
{
}
void loop1(void)
{
  if(queue.available())
  {
    //queue.pull(outdata);
  }  
  asm("wfi");
}
#endif

/*
rp2040: single preamp
rp2350: no preamps
T4.1:   dual preamps

# default speed
single channel  96 kHz rp2040 (200 MHz)    60.3mA @ 5.1V
dual channel    96 kHz rp2350 (150 MHz)    34.3mA @ 5.1V 
dual channel   384 kHz rp2350 (150 MHz)    42.3mA @ 5.1V
dual channel    96 kHz T4.1   (600 Mhz)   104.6mA @ 5.1V
dual channel   384 kHz T4.1   (600 Mhz)   114.7mA @ 5.1V

# same speed
single channel  96 kHz rp2040 (150 MHz)    54.9mA @ 5.1V  h: 1.4mA @ 5.11V  p: 825 uS (7.8%) 2nd:  827 uS (7.8%)
dual channel    96 kHz rp2350 (150 MHz)    34.3mA @ 5.1V  h: 3.6mA @ 5.11V  p: 712 uS (6.7%) 2nd: ~890 uS (8.4%)
dual channel    96 kHz T4.1   (150 Mhz)    76.5mA @ 5.1V  h: 0.5mA @ 5.11V  p: 298 uS (2.8%) n/a
*/
