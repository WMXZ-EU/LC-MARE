/* microPAM 
 * Copyright (c) 2023/2024/2025, Walter Zimmer
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

#include "src/global.h"
#include "src/rp2040.h"
#include "src/mRTC.h"
#include "src/Menu.h"
#include "src/Filing.h"
#include "src/Adc.h"

//-----------------------------------
// implementation
//-----------------------------------

status_t status=STOPPED;
uint32_t loop_count=0;
uint32_t data_count=0;

uint16_t have_disk=0;
uint16_t setup_ready=0;
uint16_t setup1_ready=0;
void setup() {
  // put your setup code here, to run once:
  // reduce MCU clock
  set_sys_clock_khz(4*12000, true);

  if(eepromLoad()==0)
  { // should load parameters from LFS or uSD (TBD)
    ;
  }

  neo_pixel_init();
  neo_pixel_show(10, 0, 0);

  while(millis()<(WAIT*1000)) if(Serial) { Serial.print(millis());break;}
  if(Serial) Serial.println("\n*********\nAdalogger");
  
  neo_pixel_show(0, 0, 0);

  for(int p=0;p<30;p++) // disable GIPOs
  { pinMode(p, INPUT); 
    gpio_set_input_enabled(p, false); 
  }
  
  rtc_setup();
  if(alarm!=0xffffffff)
  { delay(0.1);
    uint32_t tt = rtc_get();
    if(tt<alarm)
    { hibernate_until(alarm);
    }
    else
    { // clean-up initial alarm value
      eepromWrite32(11,0xffffffff);
      eepromCommit();
    }
  }

  parameterPrint();

  #if MC==0
    i2s_setup();
    dma_setup();
  #else
    setup_ready=1;
    while(!setup1_ready) delay(10);
  #endif

  have_disk=SD_init();
  if(have_disk) configShow();
  if(have_disk) status=DO_START;
  if(!have_disk)  neo_pixel_show(0, 0, 10);

  if(!Serial)
  { usb_stop();
  }

}

void loop() {
  // put your main code here, to run repeatedly:
  // management
  status = menu(status);
  if(status==DO_START)
  { adc_init();
    status=CLOSED;
  }
  if(status==JUST_STOPPED)
  { adc_exit();
    status=STOPPED;
  }
  // filing
  int32_t *buffer = is2_last_read();
  if(have_disk && buffer)
  {
    if(status != STOPPED)
    { status=logger(buffer,status);
      data_count++;
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
