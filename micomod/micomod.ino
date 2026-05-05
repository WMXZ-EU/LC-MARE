
/* microPAM 
 * Copyright (c) 2023, Walter Zimmer
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
 
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

#define START_MODE 1 // set to -1 to wait

#define DO_COMPRESS 0
#define NSAMP 128
#define NBUF 16

#define CPU_RESTART_ADDR (uint32_t *)0xE000ED0C
#define CPU_RESTART_VAL 0x5FA0004
#define CPU_RESTART (*CPU_RESTART_ADDR = CPU_RESTART_VAL)

const int32_t fsamp=44100;
const int32_t nch=1;
const int32_t t_acq=60;
const int32_t t_on=300;
const int32_t t_off=0;
const int32_t shift=14;

const char *DirPrefix = "D";
const char *FilePrefix = "F";

#include "I2S_32.h"
#include "compress.h"
#include "filing.h"

I2S_32                   acq; 
AudioRecordQueue         queue1;

#if DO_COMPRESS==1
  AudioCompress            proc;
  AudioConnection          patchCord3(acq,  0, proc, 0);
  AudioConnection          patchCord4(proc, 0, queue1, 0);
#else
  AudioConnection          patchCord4(acq, 0, queue1, 0);
#endif

void setup() {
  // put your setup code here, to run once:
  AudioMemory(60);
  acq.digitalShift(shift);

  SerNum=getTeensySerial();
  
  #if START_MODE==-1
    while(!Serial);
  #endif

  storeBegin();

  queue1.begin();
}

extern int16_t tempData0[];
void loop() {
  // put your main code here, to run repeatedly:

  static int16_t status=START_MODE;

  // basic menu to start and stop archiving
  if(Serial.available())
  {
    char ch=Serial.read();
    if(ch=='s') status=0;
    if(ch=='e') status=4;
    if(ch=='b') CPU_RESTART;
    if(ch=='z') _reboot_Teensyduino_();
    while(Serial.available()) Serial.read();
  }

  // check end of file
  status=checkEndOfFile(status);

  // if available transfer data to disk
  if(queue1.available()>=NBUF)
  { // fetch data from buffer
    for(int ii=0; ii<NBUF; ii++)
    { int16_t *buffer= queue1.readBuffer();
      memcpy(diskBuffer+ii*NSAMP, buffer, 2*NSAMP);
      queue1.freeBuffer();
    }

    // write data to disk
    //-------------------
    status=storeData(status);
  }
  
  // print some statistics every second
  static uint32_t t0=0;
  static uint32_t ic=0;
  #if DO_COMPRESS==1
    int32_t data = tempData0[0];
  #else
    int32_t data = diskBuffer[0];
  #endif
  if(millis()-t0>1000)
  { t0=millis();
    Serial.printf("\n%10d %2d %3d %.2f %6d %d",
        ic++, rtc_get()%60, AudioMemoryUsageMax(), 
        (float) fsamp/(float)(disk_count*NBUF*NSAMP), data, status);
    AudioMemoryUsageMaxReset();
    disk_count=0;
  }
}
