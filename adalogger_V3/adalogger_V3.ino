/* microPAM 
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
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
#include <stdio.h>
#include <string.h>
#include "pins_arduino.h"

#include "src/global.h"
#include "src/rp2040.h"
#include "src/mRTC.h"
#include "src/Menu.h"
#include "src/Filing.h"
#include "src/Adc.h"
#include "src/Queue.h"

#include "Wire.h"
#include "src/I2C.h"

//-----------------------------------
// implementation
//-----------------------------------

status_t status=STOPPED;
uint32_t loop_count=0;
uint32_t data_count=0;

uint16_t have_disk=0;
uint16_t setup_ready=0;
uint16_t setup1_ready=0;
char status_text[6][16]={"DO_START\0", "CLOSED\0", "RECORDING\0", "MUST_STOP\0", "JUST_STOPPED\0", "STOPPED\0"};

void setup() {
  // put your setup code here, to run once:
  // reduce MCU clock
  set_sys_clock_khz(CLK_MULT*12000, true);

  neo_pixel_init();
  neo_pixel_show(10, 0, 0);

  #if 0
  // test i2c connections
    {
      while(!Serial);
      #define ADC_EN      5
      #define ADC_SHDNZ   6

      if(1)
      { Serial.println("Power on ADC");
        pinMode(ADC_EN,OUTPUT);
        acqPower(HIGH);
        pinMode(ADC_SHDNZ,OUTPUT);
        adcReset();
        delay(100);
        adcStart();
      }

      Serial.println("wire");
      test_wire(&Wire);
      Serial.println("wire1");
      test_wire(&Wire1);
      neo_pixel_show(0, 0, 10);
      while(1);
    }  
  #endif

  if(eepromLoad()==0)
  { // should load parameters from LFS or uSD (TBD)
    // loadConfigfromFile(); // does not work; is too early
  }

  //while(!Serial);
  while(millis()<(WAIT*1000)) if(Serial) { Serial.print(millis());break;}
  if(Serial) Serial.println("\n***********\nAdalogger\n***********\n");

  neo_pixel_show(10, 10, 0);

  if (1)
  for(int p=0;p<PINS_COUNT;p++) // disable GIPOs (to save power,hopefully)
  { if(p==PIN_NEOPIXEL) continue; // neopixel
    #if MCU==RP_2350
      if(p==RP2350_PSRAM_CS) continue; // psram
    #endif
    pinMode(p, INPUT); 
    gpio_set_input_enabled(p, false); 
  }
  
  // set mcu rtc
  // put it to some time so it is running
  // it will be synchronized to external rtc later
  if(1)
  { const datetime_t setTime = { 2026, 1, 1, 4, 0, 0, 0 };
    rtc_init();
    rtc_set_datetime( &setTime); 
    Serial.print("rtc running "); Serial.println(rtc_running());
    //
    datetime_t t;
    rtcGetDatetime(&t);
    printDatetime("rtc",&t);
  }

  #if 0  // check time stamp
    // activate for testing rtc
    while(1)
    { // for testing
      delay(1000);
      datetime_t t;
      rtcGetDatetime(&t);
      printDatetime("rtc",&t);
    }
  #endif

  // set-up external rtc
  int xrtc=0;
  xrtc=rtc_setup();
  Serial.print("xrtc "); Serial.println(xrtc);

  #if 0 // check times
    // sync is done in mRTC.cxx
    if(xrtc)
    {
      datetime_t t;
      XRTCgetDatetime(&t);  
      rtcSetDatetime(&t);  
      Serial.println("Corrected time is");
      printDatetime("rtc",&t);
    }
  #endif

  if(alarm!=0xffffffff)
  { delay(0.1);
    Serial.print("alarm "); Serial.println(alarm);
    uint32_t tt = rtc_get();
    if(xrtc && (tt<alarm))  // only hibernate if xrtc exists
    { neo_pixel_show(0, 0, 0);
      hibernate_until(alarm);
    }
    else
    { // clean-up initial alarm value
      // as there is no external rtc or we passed alarm time
      eepromUpdateAlarm(0xffffffff);
    }
  }

  Serial.println("Parameter Print");
  parameterPrint();

  #if MC==0
    // have single core; start acquisition here
    i2s_setup();
    dma_setup();
  #else
    // have dual core; release and wait for setup of second core 
    setup_ready=1;
    while(!setup1_ready) delay(10);
  #endif
  //
  Serial.printf("PSRAM Size: %d\r\n", rp2040.getPSRAMSize());
  Serial.printf("Queue Size: %d\r\n",  MAX_QUEUE*MD*NBUF_I2S*4);

  have_disk=SD_init();
  Serial.print("have disk: "); Serial.println(have_disk);
  if(have_disk) configShow();
  if(have_disk) status=DO_START;
  if(!have_disk)  neo_pixel_show(0, 0, 10); else neo_pixel_show(0, 0, 0);

  if(!Serial)
  { //usb_stop(); // may be useful to cut power consumption further but hinders development
  }
  Serial.print("status: ");Serial.println(status_text[status]);
}

extern uint32_t diskBuffer[];
void loop() {
  // put your main code here, to run repeatedly:
  //
  // management
  //status_t old_status=status;
  status = menu(status);
  if(status==DO_START)
  { //Serial.print("status: ");Serial.println(status_text[old_status]);
    adc_init();
    status=CLOSED;
    neo_pixel_show(0, 0, 0);
  }
  if(status==JUST_STOPPED)
  { adc_exit();
    status=STOPPED;
    neo_pixel_show(0, 10, 0);
  }
  //
  // filing
  int ndc=getQueueCount();
  if(ndc>0)
  { if(have_disk)
    { // write data to disk to clean up
      if(status != STOPPED)
      { 
        status=logger(status);
        data_count++;
      }
      else
      {
        pullQueue(diskBuffer);
      }
    }
    else
    { // write some data every second to screen
      pullQueue(diskBuffer);

      if(status < MUST_STOP)
      { 
        static uint32_t t0=0;
        if(millis()>t0+1000)
        { t0=millis();
          Serial.print(acq_count); Serial.print(" ");Serial.print(missed_acq); Serial.print(" "); acq_count=0; missed_acq=0;
          Serial.print(ndc); Serial.print(": ");
          for(int ii=0;ii<8;ii++) Serial.printf("%08x ",diskBuffer[ii]); Serial.println();
        }
      }
      // simulate stopping process
      if(status==MUST_STOP) {status=JUST_STOPPED; Serial.println("\njust stopped");}
    }
  }
  //
  loop_count++;
  asm("wfi");
}

#if MC==1
  void setup1(void)
  { while(!setup_ready) delay(100);
    Serial.println("setup1");
    i2s_setup();
    dma_setup();
    setup1_ready=1;
  }

  void loop1(void)
  {
    asm("wfi");
  }
#endif
