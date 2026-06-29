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
//
#include <Arduino.h>

#include "src/global.h"
#if MCU==T_4_1
  #include "src/Teensy.h"
#else
  #include "src/rp2x.h"
#endif

#include "src/adc.h"
#include "src/rtc.h"
#include "src/process.h"
#include "src/classifier.h"
#include "src/menu.h"
#include "src/filing.h"
#include "src/utils.h"
/********************************* basics *******************************************************/
//  enum status_t  {DO_START, CLOSED, RECORDING, MUST_STOP, JUST_STOPPED, STOPPED, MUST_HIBERNATE};
char status_text[7][16]=
  {"DO_START\0", "CLOSED\0", "RECORDING\0", "MUST_STOP\0", "JUST_STOPPED\0", "STOPPED\0", "MUST_HIBERNATE\0"};

status_t status=STOPPED;

volatile uint32_t fsamp=FSAMP;

#if MCU == T_4_1
  void acqSetup(void)
  { if(fsamp>FSAMP) fsamp=FSAMP; 
    dsp_init();
    acqInit(fsamp);
    acqStart();
  }
#else
  void core1_main(void) 
  { if(fsamp>FSAMP) fsamp=FSAMP; 
    acqInit(fsamp);
    acqStart();
    while (true) {asm("wfi");}
  }

  void acqSetup(void)
  { // Launch core 1
    multicore_launch_core1(core1_main);
  }
#endif
/********************************* main *********************************************************/
void setup() {
  // put your setup code here, to run once:
  //set_MCU_clock(CLK_MULT);

  while(millis()<5000) if(Serial) break;
  if(Serial)
  { Serial.println("\n*** micoPAM ***");
    Serial.print("millis: "); Serial.println(millis());
  }

  printCrashReport();
  //
  getUID();
  Serial.print("uid ");Serial.println(uid_strng);

  systemInit();

  rtc_setup();
  //
  have_disk=SD_init();
  Serial.print("have disk: "); Serial.println(have_disk);
  configShow();

  queue.reset();
  process_init();
  //
  acqSetup();

  status=STOPPED;
  status=CLOSED;
}

//extern float D[];
volatile uint32_t loop_count=0;
volatile uint32_t loop1_count=0;
volatile uint32_t loop1_timer=0;

void printMonitor(const char *type, uint32_t cnt, uint32_t *loop_count, uint32_t * buffer)
{ if(!Serial) return;
//            Serial.printf("1- %d %d %3d %3d %4d us %.3f ms (%4.1f%%) ",cnt++, acq_count, fsamp*NCHAN_I2S/NBUF_I2S, acq_missed, 
//                    process_max_us, (1000.0f*NBUF_I2S)/(fsamp*NCHAN_I2S),process_max_us/(10000.0f*acq_count));
//            Serial.printf("%2d: ",loop1_count);
  Serial.print(type); Serial.print(cnt); 
  Serial.print(" "); Serial.print(acq_count);
  Serial.print(" "); Serial.print( (fsamp*NCHAN_I2S)/NBUF_I2S);
  Serial.print(" "); Serial.print(acq_missed);
  Serial.print(" "); Serial.print((1000.0f*NBUF_I2S)/(fsamp*NCHAN_I2S));
  Serial.print(" "); Serial.print(process_max_us);
  #if MCU==T_4_1
  Serial.print(" "); Serial.print(dsp_isr_max_us);
  Serial.print(" "); Serial.print(classifier_exec_us);
  #endif
  acq_count=0;
  acq_missed=0;
  process_max_us=0;
  #if MCU==T_4_1
  dsp_isr_max_us=0;
  #endif
  Serial.print(" "); Serial.print(*loop_count);
  *loop_count=0;
  #if MCU==T_4_1
  Serial.print(" "); printFloat(Imax,3);
  Imax=0.0f;
  #endif
  Serial.print(": ");
  for(int ii=0;ii<4;ii++)  { Serial.print(" "); printHex32(buffer[ii],0);}
  #if (MCU==T_4_1) && (PROC_MODE==4)
    Serial.print(" mu:");
    for(int ii=0;ii<VAE_LAT_TOTAL;ii++) { Serial.print(" "); Serial.print(vae_mu[ii],4); }
    Serial.print(" sig:"); Serial.print(vae_mu_signal_count);
    if(vae_mu_signal_count > 0)
    { Serial.print(" mu_sig:");
      for(int ii=0;ii<VAE_LAT_TOTAL;ii++) { Serial.print(" "); Serial.print(vae_mu_signal[ii]/vae_mu_signal_count,4); }
      for(int ii=0;ii<VAE_LAT_TOTAL;ii++) vae_mu_signal[ii]=0.0f;
      vae_mu_signal_count=0;
    }
  #else
    for(int ii=4;ii<10;ii++) { Serial.print(" "); printHex32(buffer[ii],1);}
  #endif
  Serial.println();
}

void loop() {
  // put your main code here, to run repeatedly:
  static uint32_t cnt;

  status = menu(status);
  if(status==DO_START)
  { //Serial.print("status: ");Serial.println(status_text[old_status]);
    adc_init();
    status=CLOSED;
  }
  if(status==JUST_STOPPED)
  { adc_exit();
    status=STOPPED;
  }
  if(status==MUST_HIBERNATE)
  { // 

  }

  if(queue.available()>0)
  {
    if(have_disk)
    { // 
      if(status != STOPPED) // have disk and started acquisition
      { status =logger(status);
        //
        #if MONITOR==1 // to monitor during writing
          { loop1_count++;
            //
            static uint32_t to=0;
            if(millis()>to+1000)
            { to=millis();
              for(int ii=0; ii<10;ii++) logBuffer[ii]=diskBuffer[ii];
              printMonitor("1 - ", cnt++, (uint32_t *)&loop1_count, logBuffer);
            }
          }
        #endif
      }
      else // have disk but stopped
      {
        queue.pull(diskBuffer);
      }
    } 
    else  // have no disk
    {
      queue.pull(diskBuffer);

      if(status < MUST_STOP) // started acquisition but no disk
      {
        loop_count++;
        static uint32_t to=0;
        if(millis()>to+1000)
        { to=millis();
          for(int ii=0; ii<10;ii++) logBuffer[ii]=diskBuffer[ii];
          printMonitor("0 - ", cnt++, (uint32_t *) &loop_count, logBuffer);
        }
      }
      // simulate stopping process (in case there is no disk)
      if(status==MUST_STOP) {status=JUST_STOPPED; Serial.println("\njust stopped");}
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

/*
Some performance numbers

rp2040: single preamp 1B34342E
rp2350: no preamps    D6919ED7
T4.1:   dual preamps  E51382FB

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

#cpu 150 MHz (384/192 kHz)
mcu     acq   disk nb      mA      proc(us)
rp2040  192   12    e    71.5 ( 840; 15.8%)
rp2350  755   36    a    76.5 ( 780; 29.1%)
T4.1    755   51    e   134.5 ( 369; 11.7%)

150 MHz (192 kHz)
rp2040  192   12    e    71.5 ( 840; 15.8%) 
rp2350  378   18    a    57.3 ( 782; 14.7%) 
T4.1    382   26    e   128.0 ( 305;  6.8%) 

T4_1
 4chan 192 kHz 600MHz
  388 acq/s
 no procesing 
  343 us  (6.2%)  175mA
 4 spectra 50% overlap
  600 us  (12.2%) 179mA

comment previous % may be wrong estimates
-------------------------------------------------------------------
TD-1.61
32-06-26 15:00 4-chan 192 kHz
RP2350(150 MHz)  755 750 743 us 1.333 ms (56.1%) 36  (no dsp_apply)
T4.1(600 MHz)    376 375 131 us 2.667 ms ( 4.9%) 27  (no dsp_apply)
T4.1(600 MHz)    380 375 565 us 2.667 ms (21.5%) 32  (with dsp_apply)
*/
