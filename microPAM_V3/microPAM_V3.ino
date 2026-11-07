/* microPAM_V3
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
// Passive acoustic recorder for Adafruit Feather RP2040 Adalogger / RP2350 HSTX
// with TLV320ADC6140 ADC and RV-3028 RTC (LC-MARE), and for Teensy 4.1 with two
// TLV320ADC6140 and the SNVS RTC.
//
// RP:     core 1: PIO/DMA acquisition, (PROC_MODE 1) compression, push to queue
//         core 0: menu, queue -> microSD, duty cycle and hibernation
// Teensy: DMA ISR: acquisition, compression, push to queue; loop(): as RP core 0
//
#include "src/global.h"
#include "src/rp2x.h"
#if MCU==T_4_1
  #include "src/teensy.h"
#endif
#include "src/rtc.h"
#include "src/menu.h"
#include "src/filing.h"
#include "src/adc.h"
#include "src/queue.h"

const char status_text[][16] = {"DO_START", "CLOSED", "RECORDING", "MUST_STOP", "JUST_STOPPED", "STOPPED"};

static status_t status = STOPPED;
uint32_t loop_count = 0;
uint32_t data_count = 0;

static uint16_t have_disk = 0;
static volatile uint16_t setup_ready = 0;
static volatile uint16_t setup1_ready = 0;

void setup()
{
  #if MCU==T_4_1
    lowPowerInit();
    #if PHY_OFF
      phyPowerDown();
    #endif
    #if T41_POWER
      powerSteps(T41_POWER);
    #endif
  #else
    set_sys_clock_khz(CLK_MULT*12000, true);
  #endif

  neo_pixel_init();
  neo_pixel_show(10, 0, 0);

  // woken by the RTC alarm: start recording without waiting for USB
  #if MCU==T_4_1
    int alarm_wake = alarmWake();               // SNVS alarm flag (power-on by alarm)
  #else
    pinMode(XRTC_INT_PIN, INPUT_PULLUP);
    int alarm_wake = !digitalRead(XRTC_INT_PIN);
  #endif
  if(alarm_wake)
  { status = DO_START;
  }
  else
  { while(millis() < (WAIT*1000)) if(Serial) break;
    if(Serial) delay(200);    // let the host start reading, or the first lines are lost
  }
  Serial.println("\n***********\n" Program " " Version " (" MCU_NAME ")\n***********\n");
  neo_pixel_show(10, 10, 0);

  #if MCU==T_4_1
    printCrashReport();
  #else
    // disable unused GPIOs to save power
    for(int p = 0; p < (int)PINS_COUNT; p++)
    { if(p == PIN_NEOPIXEL) continue;
      if(p == XRTC_INT_PIN) continue;
      #if MCU==RP_2350
        if(p == RP2350_PSRAM_CS) continue;
      #endif
      pinMode(p, INPUT);
      gpio_set_input_enabled(p, false);
    }
  #endif

  int xrtc = rtc_setup();
  Serial.print("xrtc "); Serial.println(xrtc);

  // release the INT pin, so that the next boot is not taken for an alarm wake-up
  int alarm_flag = XRTCclearAlarm();
  Serial.printf("XRTC INT at boot: %s; alarm flag %d (cleared)\n", alarm_wake ? "low" : "high", alarm_flag);

  have_disk = SD_init();
  Serial.print("have disk: "); Serial.println(have_disk);

  Serial.println("\nConfiguration Parameters");
  configShow();

  // delayed start: sleep until start time (if within the next year)
  Serial.print("Start Time: "); Serial.println(startTime);
  datetime_t tm;
  decodeTimestamp(&tm, startTime);
  uint32_t to = date2time(&tm, RTC_EPOCH);
  uint32_t tt = rtc_get();
  Serial.println(tt);
  Serial.println(to);
  if((tt < to) && (to < tt + 365*24*3600))
  { neo_pixel_show(0, 0, 0);
    hibernate_until(to);
  }

  if(!have_disk)
  { neo_pixel_show(0, 0, 10);
  }
  else
  { neo_pixel_show(0, 0, 0);
    status = DO_START;
  }

  #if MCU==T_4_1
    // single core: start acquisition here
    Serial.println("acquisition setup");
    queueInit();
    i2s_setup();
    dma_setup();
  #else
    // release acquisition core and wait for it
    setup_ready = 1;
    while(!setup1_ready) delay(10);
  #endif

  Serial.print("status: "); Serial.println(status_text[status]);
}

void loop()
{
  status = menu(status);
  if(status == DO_START)
  { adc_init();
    status = CLOSED;
    neo_pixel_show(0, 0, 0);
  }
  if(status == JUST_STOPPED)
  { adc_exit();
    status = STOPPED;
    neo_pixel_show(0, 10, 0);
  }

  int ndc = getQueueCount();
  if(ndc > 0)
  { if(have_disk)
    { if(status != STOPPED)
      { status = logger(status);    // counts data_count per block written
      }
      else
      { pullQueue(diskBuffer);   // discard
      }
    }
    else
    { // no disk: print first words of a block once per second
      pullQueue(diskBuffer);
      if(status < MUST_STOP)
      { static uint32_t t0 = 0;
        if(millis() > t0 + 1000)
        { t0 = millis();
          Serial.printf("%lu %lu %d: ", acq_count, missed_acq, ndc);
          acq_count = 0; missed_acq = 0;
          for(int ii = 0; ii < 8; ii++) Serial.printf("%08lx ", diskBuffer[ii]);
          Serial.println();
        }
      }
      if(status == MUST_STOP) { status = JUST_STOPPED; Serial.println("\njust stopped"); }
    }
  }
  loop_count++;
  asm("wfi");
}

#if MCU!=T_4_1
// core 1: acquisition (DMA IRQ is bound to the core that enables it)
void setup1()
{
  while(!setup_ready) delay(100);
  Serial.println("setup1");
  i2s_setup();
  dma_setup();
  setup1_ready = 1;
}

void loop1()
{
  asm("wfi");
}
#endif
