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
#include "Arduino.h"

#include "global.h"
#include "Menu.h"
#include "mRTC.h"
#include "rp2040.h"
#include "Adc.h"
#include "Filing.h"

void eepromUpdate();
void eepromList();
static uint16_t eeprom=0;

uint32_t alarm=0xffffffff;
void eepromWrite32(byte a, uint32_t v);
void eepromCommit();

void parameterPrint(void)
{ Serial.println("\n====================");
  Serial.println(Program);
  Serial.print("Version    "); Serial.println(Version);
  getUID();
  Serial.print("UID        "); Serial.println(uid_str);
  Serial.print("eeprom (w) "); Serial.print(eeprom); Serial.println();
  Serial.print("t_acq  (a) "); Serial.print(t_acq);  Serial.println(" sec");
  Serial.print("t_on   (o) "); Serial.print(t_on);   Serial.println(" min");
  Serial.print("t_rep  (r) "); Serial.print(t_rep);  Serial.println(" min");
  Serial.print("fsamp  (f) "); Serial.print(fsamp);  Serial.println(" Hz");
  Serial.print("again  (g) "); Serial.print(again);  Serial.println(" dB");
  Serial.print("Processing "); Serial.println(PROC);

  eepromList();

  configShow();
}

// User Interface
static char * menuGetLine(void)
{
  static char buffer[40];
  while(!Serial.available()) continue;
  Serial.setTimeout(5000);
  int count;
  count = Serial.readBytesUntil('\r',buffer,40);
  buffer[count]=0;
  Serial.println(buffer);
  return buffer;
}
static int menuGetInt16(uint16_t *val)
{ char *buffer=menuGetLine();
  int tmp;
  sscanf(buffer,"%d",&tmp); *val=(uint16_t) tmp;
  return 1;
}
static int menuGetInt32(uint32_t *val)
{ char *buffer=menuGetLine();
  int tmp;
  sscanf(buffer,"%d",&tmp); *val=(uint32_t) tmp;
  return 1;
}
static uint16_t menuGetTime(datetime_t *t)
{
  char *buffer=menuGetLine();
  int v1,v2,v3,v4,v5,v6;
  char c1,c2,c3,c4,c5;

  if(strlen(buffer)<19) return 0;
  //
  sscanf(buffer,"%d%c%d%c%d%c%d%c%d%c%d",
                &v1,&c1, &v2,&c2, &v3,&c3, &v4,&c4, &v5,&c5, &v6);
  t->year =v1;
  t->month=v2;
  t->day  =v3;
  t->hour =v4;
  t->min  =v5;
  t->sec  =v6;
  return 1;
}
status_t menu(status_t status)
{
  if(Serial)
  {
    if(Serial.available())
    {
      char ch;
      ch=Serial.read();
      if(ch=='s') // start acquisition
      { Serial.println("start");
        status=DO_START;
      }
      else if(ch=='e')  // stop aquisition
      { Serial.print("stop ");
        status=MUST_STOP;
      }
      else if(ch=='p')  // print parameters
      {  parameterPrint();
      }
      else if(ch=='x')  //exit and sleep until next hour
      { uint16_t h_off; 
        menuGetInt16(&h_off);
        if(h_off>0)
        { Serial.print(" hibernating "); Serial.print(h_off); Serial.print(" hours");
          alarm = rtc_get();
          alarm /= 3600;
          alarm = (alarm+h_off)*3600;
          Serial.print(" ("); Serial.print(alarm-rtc_get()); Serial.println(" sec)");
          eepromWrite32(11,alarm);
          eepromCommit();
          hibernate_until(alarm);
        }
        else
        {
            reboot();
        }
      }
      else if(ch=='?')  // get parameter
      {
        while(!Serial.available()) delay(10);
        ch=Serial.read();
        if(ch=='a') 
        { Serial.print("a = "); Serial.println(t_acq); 
        }
        if(ch=='o') 
        { Serial.print("o = "); Serial.println(t_on);
        }
        if(ch=='r') 
        { Serial.print("r = "); Serial.println(t_rep);
        }
        if(ch=='f') 
        { Serial.print("f = "); Serial.println(fsamp);
        }
        if(ch=='g') 
        { Serial.print("g = "); Serial.println(again);
        }
        if(ch=='w') 
        { Serial.print("w = "); Serial.println(eeprom);
        }
      }
      else if(ch=='!')  // modify parameters
      {
        while(!Serial.available()) delay(10);
        ch=Serial.read();
        if(ch=='a') 
        { menuGetInt16((uint16_t*)&t_acq); 
          }
        if(ch=='o') 
        { menuGetInt16((uint16_t*)&t_on);
        }
        if(ch=='r') 
        { menuGetInt16((uint16_t*)&t_rep);
        }
        if(ch=='f') 
        { menuGetInt32((uint32_t*)&fsamp);
          acqModifyFrequency(fsamp);        
        }
        if(ch=='g') 
        { menuGetInt16((uint16_t*)&again);
          setAGain((int8_t)again&0xff);
        }
        if(ch=='w') 
        { menuGetInt16((uint16_t*)&eeprom);
          eepromUpdate();
        }
      }
      else if(ch=='c')  // check and correct RTC time
      {
        if(status==STOPPED) // only if we are stopped
        {
          while(Serial.available()) {volatile char c = Serial.read(); (void) c;}
          // print time stamp
          datetime_t t;
          XRTCgetDatetime(&t);    
          Serial.println("Actual time on XRTC is");
          printDatetime("  ",&t);
          Serial.println("If correct press return, otherwise enter correct date and time");

          // correct RTC time is required
          if(menuGetTime(&t))
          {
            XRTCsetDatetime(&t);  
            delay(10);
            // write also to local rtc
            rtcSetDatetime(&t);  
            Serial.println("Corrected time is");
            printDatetime("rtc",&t);
          }
          else
          {
            Serial.println("Time has not been corrected");
          }
        }
      }
    }
  }
  return status;
}

/**********************Parameters***********************/
#include <EEPROM.h>

void eepromInit() { EEPROM.begin(256);}
void eepromCommit() {  EEPROM.commit();}

void eepromWrite(byte a, uint32_t v)
{
    EEPROM.write(a,v&0xff);
    EEPROM.write(a+1,(v>>8) &0xff);
}
uint16_t eepromRead(byte a)
{ uint16_t v;
    v = EEPROM.read(a);
    v |= EEPROM.read(a+1)<<8;
    return v;
}

void eepromWrite32(byte a, uint32_t v)
{
    EEPROM.write(a,v&0xff);
    EEPROM.write(a+1,(v>>8) &0xff);
    EEPROM.write(a+2,(v>>16) &0xff);
    EEPROM.write(a+3,(v>>24) &0xff);
}
uint32_t eepromRead32(byte a)
{ uint32_t v;
    v = EEPROM.read(a);
    v |= EEPROM.read(a+1)<<8;
    v |= EEPROM.read(a+2)<<16;
    v |= EEPROM.read(a+3)<<24;
    return v;
}

void eepromUpdate() 
{ 
  eepromWrite(1,t_acq);
  eepromWrite(3,t_on);
  eepromWrite(5,t_rep);
  eepromWrite(7,fsamp/1000);
  eepromWrite(9,again);
  EEPROM.write(0,eeprom&0xff);
  EEPROM.commit();
}
void eepromList(void)
{  for(int ii=0;ii<16;ii++) {Serial.print(EEPROM.read(ii)); Serial.print(' ');} Serial.println();
}

uint16_t eepromLoad(void)
{ // load parameters from EEPROM
  eepromInit();
  alarm = eepromRead32(11);
  Serial.println(alarm,HEX);

  eeprom=EEPROM.read(0);
  if(eeprom==1)
  { 
    t_acq = eepromRead(1);
    t_on  = eepromRead(3);
    t_rep = eepromRead(5);
    fsamp = eepromRead(7)*1000;
    again = eepromRead(9);
    return 1;
  }
  return 0;
}
