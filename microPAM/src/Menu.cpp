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
 #include "Arduino.h"
#include "Config.h"
#include "Menu.h"
#include "RTC.h"
#include "Acq.h"

static uint16_t store[16] = {0};

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

static int menuGetInt16(int16_t *val)
{ char *buffer=menuGetLine();
  int tmp;
  sscanf(buffer,"%d",&tmp); *val=(int16_t) tmp;
  return 1;
}

static int menuGetInt32(int32_t *val)
{ char *buffer=menuGetLine();
  int tmp;
  sscanf(buffer,"%d",&tmp); *val=(int32_t) tmp;
  return 1;
}

static int menuGet3Int(int *val1, int *val2, int *val3)
{ char *buffer=menuGetLine();
  char c1,c2;
  return sscanf(buffer,"%d%c%d%c%d",val1,&c1,val2,&c2,val3);
}

#include "Filing.h"
void resetMTP();
void resetUSB(void);
void adcStatus(void);
void setAGain(int8_t again);
void reboot(void) ;


int16_t monitor=0;
int16_t menu(int16_t status)
{
  // basic menu to start and stop archiving  
  if(Serial.available())
  {
    char ch=Serial.read();
    // some basic options (without ":")
    if(ch=='s') {Serial.print("\n Start"); adcStatus(); status=CLOSED;}
    else if(ch=='e') {Serial.print("\n Stop"); status=MUSTSTOP;}
    else if(ch=='m') {monitor=1-monitor; Serial.print("\n Monitor "); Serial.print(monitor); }
    else if(ch=='r') {resetUSB();}
    else if(ch=='b') {reboot();}
    else if(ch=='x') {powerDown();}
    // detailed options
    else if(ch==':') status=menu1(status); 
    else if(ch=='?') menu2(); 
    else if(ch=='!') menu3(); 
//    else Serial.clear();
    else {Serial.print(ch); Serial.println("\nAllowed commands: 's','e','m','r'. See also '?p' for parameters"); }
    while(Serial.available()) { ch=Serial.read(); Serial.print(ch);} // clean-up
  }

  return status;
}

int16_t menu1(int16_t status)
{   // have ':'
    while(!Serial.available()) ;
    char ch;
    ch=Serial.read();
    if(ch=='w') 
    { Serial.println("Save parameters");
      saveParameters();
    }
    else if(ch=='m') // control monitor (needed for gui)
    {
      menuGetInt16((int16_t *)&monitor);
    }
  #if defined(__IMXRT1062__)
    else if(ch=='c') // transfer internal rtc to external rtc
    { rtcXferTime();
    }
  #endif
    return status;
}

void menu2(void)
{   // have '?'
    datetime_t t;

    while(!Serial.available()) ;
    char ch;
    ch=Serial.read();
    if(ch=='p') // get parameters
    {
      Serial.println();
      Serial.println(version);

      rtc_get_datetime(&t);
      Serial.printf("Now:\n%4d-%02d-%02d %02d:%02d:%02d %d\n",
                   t.year,t.month,t.day,t.hour,t.min,t.sec,t.dotw);
      
  #if defined(__IMXRT1062__)
      Serial.println(rtcGetTimestamp());
      #endif
      Serial.print("t_acq (a) = "); Serial.println(t_acq);
      Serial.print("t_on  (o) = "); Serial.println(t_on);
      Serial.print("t_rep (r) = "); Serial.println(t_rep);
      Serial.print("fsamp (f) = "); Serial.println(fsamp);
      Serial.print("shift (s) = "); Serial.println(shift);
      Serial.print("proc  (c) = "); Serial.println(proc);
      Serial.print("again (g) = "); Serial.println(again);
      Serial.print("t_1   (1) = "); Serial.println(t_1);
      Serial.print("t_2   (2) = "); Serial.println(t_2);
      Serial.print("t_3   (3) = "); Serial.println(t_3);
      Serial.print("t_4   (4) = "); Serial.println(t_4);
    }
    else if(ch=='d') // get date
    {
      rtc_get_datetime(&t);
      Serial.printf("date (d) = %4d-%02d-%02d\n", t.year,t.month,t.day);
    }
    else if(ch=='t') // get time
    {
      rtc_get_datetime(&t);
      Serial.printf("time (t) = %02d:%02d:%02d\n", t.hour,t.min,t.sec);
    }
    else if(ch=='a')
    {
      Serial.print("t_acq (a) = "); Serial.println(t_acq);
    }
    else if(ch=='o')
    {
      Serial.print("t_on (o)  = "); Serial.println(t_on);
    }
    else if(ch=='r')
    {
      Serial.print("t_rep (r) = "); Serial.println(t_rep);
    }
    else if(ch=='f')
    {
      Serial.print("fsamp (f) = "); Serial.println(fsamp);
    }
    else if(ch=='s')
    {
      Serial.print("shift (s) = "); Serial.println(shift);
    }
    else if(ch=='c')
    {
      Serial.print("proc  (c) = "); Serial.println(proc);
    }
    else if(ch=='g')
    {
      Serial.print("again (g) = "); Serial.println(again);
    }
    else if(ch=='1')
    {
      Serial.print("t_1  (1) = "); Serial.println(t_1);
    }
    else if(ch=='2')
    {
      Serial.print("t_2  (2) = "); Serial.println(t_2);
    }
    else if(ch=='3')
    {
      Serial.print("t_3  (3) = "); Serial.println(t_3);
    }
    else if(ch=='4')
    {
      Serial.print("t_4  (4) = "); Serial.println(t_4);
    }
    else if(ch=='w')
    { uint16_t *params=loadParameters();
      Serial.print("params[0]  = "); Serial.println(params[0]);
    }
    while(Serial.available()) ch=Serial.read();

}

void menu3(void)
{   // have '!'

    while(!Serial.available()) ;
    char ch;
    ch=Serial.read();
    if(ch=='d') // set date
    { int year,month,day;
      menuGet3Int(&year,&month,&day);

      rtcSetDate(year,month,day);
    } 
    else if(ch=='t') // set time
    { int hour,minutes,seconds;
      menuGet3Int(&hour,&minutes,&seconds);
      //
      rtcSetTime(hour,minutes,seconds);
    } 
    else if(ch=='a')
    {
      menuGetInt32((int32_t *)&t_acq);
    }
    else if(ch=='o')
    {
      menuGetInt32((int32_t *)&t_on);
    }
    else if(ch=='r')
    {
      menuGetInt32((int32_t *)&t_rep);
    }
    else if(ch=='f')
    {
      menuGetInt32((int32_t *)&fsamp);
      acqModifyFrequency(fsamp);
    }
    else if(ch=='s')
    {
      menuGetInt16((int16_t *)&shift); if(shift<0) shift=0;
    }
    else if(ch=='c')
    {
      menuGetInt16((int16_t *)&proc);
    }
    else if(ch=='g')
    {
      menuGetInt16((int16_t *)&again);
      setAGain(again);
    }
    else if(ch=='w')
    { 
      menuGetInt16((int16_t *)&store[0]);
    }
    else if(ch=='1')
    { 
      menuGetInt16((int16_t *)&t_1);
    }
    else if(ch=='2')
    { 
      menuGetInt16((int16_t *)&t_2);
    }
    else if(ch=='3')
    { 
      menuGetInt16((int16_t *)&t_3);
    }
    else if(ch=='4')
    { 
      menuGetInt16((int16_t *)&t_4);
    }
}

/******************** Parameter ******************************/
void storeConfig(uint16_t *store, int ns)
{ 
  #if defined(__IMXRT1062__)
    eeprom_write_block(store, 0, ns*sizeof(store[0]));  
  #endif
}

void loadConfig(uint16_t *store, int ns)
{
  #if defined(__IMXRT1062__)
    eeprom_read_block(store, 0, ns*sizeof(store[0]));  
  #endif
}

void saveParameters(void)
{
  store[1]  = t_acq;
  store[2]  = t_on;
  store[3]  = t_off;
  store[4]  = t_rep;
  store[5]  = proc;
  store[6]  = shift;
  store[7]  = t_1;
  store[8]  = t_2;
  store[9]  = t_3;
  store[10] = t_4;
  store[11] = fsamp/1000;
  store[12] = again;
  store[13] = dgain;
  store[14] = 0;
  store[15] = 0;

  storeConfig(store, 16);
}

uint16_t *loadParameters(void)
{
  loadConfig(store,16);
  if(store[0]==1)
  {
    t_acq   = store[1];
    t_on    = store[2];
    t_off   = store[3];
    t_rep   = store[4];
    proc    = store[5];
    shift   = store[6];
    t_1     = store[7];
    t_2     = store[8];
    t_3     = store[9];
    t_4     = store[10];
    fsamp   = store[11]*1000;
    again   = store[12];
    dgain   = store[13];
  }
  else
  {
    store[0]  = 0;
    store[1]  = t_acq    = T_ACQ;
    store[2]  = t_on     = T_ON;
    store[3]  = t_off    = T_OFF;
    store[4]  = t_rep    = T_REP;
    store[5]  = proc     = PROC_MODE;
    store[6]  = shift    = SHIFT;
    store[7]  = t_1      = T_1;
    store[8]  = t_2      = T_2;
    store[9]  = t_3      = T_3;
    store[10] = t_4      = T_4;
    store[11] = (fsamp   = FSAMP)/1000; 
    store[12] = again    = AGAIN;
    store[13] = dgain    = DGAIN;
    store[14] = 0;
    store[15] = 0; 
  }
  return store;
}

uint16_t *getStore(void) {return store;}
