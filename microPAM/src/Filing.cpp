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

#include <SdFat.h>

#include "Config.h"
#include "Queue.h"
#include "Acq.h"
#include "Adc.h"
#include "RTC.h"
#include "Menu.h"
#include "Filing.h"

#define MAGIC "WMXZ"

volatile uint32_t t_acq= T_ACQ;
volatile uint32_t t_on = T_ON;
volatile uint32_t t_off= T_OFF;
volatile uint32_t t_rep= T_REP;
volatile uint32_t t_1  = T_1;
volatile uint32_t t_2  = T_2;
volatile uint32_t t_3  = T_3;
volatile uint32_t t_4  = T_4;

#if SDFAT_FILE_TYPE != 3
 #error "SDFAT_FILE_TYPE != 3: edit SdFatConfig.h"
#endif

#if defined(TARGET_RP2040)
  const int _MOSI = 3;
  const int _MISO = 4;
  const int _CS   = 5;
  const int _SCK  = 6;

  #if ENABLE_DEDICATED_SPI !=1
    #error "ENABLE_DEDICATED_SPI !=1: edit SdFatConfig.h"
  #endif

  // Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
  #define SD_CONFIG SdSpiConfig(_CS, DEDICATED_SPI, SD_SCK_MHZ(25))

  extern "C" void flash_get_unique_id(uint8_t *p);
  uint32_t UniqueID[2];
  SdFs sdx[1];
  void storage_vonfigure(void){}

#elif defined(__IMXRT1062__)
  #define SD_CONFIG SdioConfig(FIFO_SDIO)
  extern SdFs sdx[];
#endif

SdFs *sd = &sdx[0];

//SdFs sd;
FsFile file;

static HdrStruct wav_hdr;

volatile int haveStore=0;
uint32_t diskSpace=0;
uint32_t diskSize=0;

// define the number of audio blocks to load for writing to disk
// data are acquired always with 32 bit
// if data are compressed (PROC_MODE=1) then always 32 bit mode (max disk buffer) is used
// if wav files with lower bit width are used then disk buffer size is reduced accordingly
#if (NBITS==32) || (PROC_MODE==1)
  const int nblocks=NDBL;
#elif NBITS==24
  const int nblocks=3*NDBL/4;
#elif NBITS==16
  const int nblocks=NDBL/2;
#endif
    

// tempBuffer to keep all 32-bit data 
#define MAX_TEMP_BUFFER (NDBL*NBUF_ACQ)
static int32_t tempBuffer0[MAX_TEMP_BUFFER];
//static int32_t tempBuffer1[MAX_TEMP_BUFFER];

// diskBuffer to hold all data to be written to disk (with and without compression
#define MAX_DISK_BUFFER (nblocks*NBUF_ACQ)
static int32_t diskBuffer[MAX_DISK_BUFFER];
uint32_t disk_count=0;

uint32_t SerNum=0;

// Call back for file timestamps.  Only called for file create and sync(). needed by SDFat
void dateTime(uint16_t* date, uint16_t* time, uint8_t* ms10) 
{
    datetime_t t;
    rtc_get_datetime(&t);

    *date = FS_DATE(t.year,t.month,t.day);
    *time = FS_TIME(t.hour,t.min,t.sec);
    *ms10 = 0;
}

int16_t filing_init(void)
{
  #if defined(TARGET_RP2040)
    SPI.setRX(_MISO);
    SPI.setTX(_MOSI);
    SPI.setSCK(_SCK);
    SPI.setCS(_CS);

    flash_get_unique_id((uint8_t *) UniqueID);
    SerNum=UniqueID[1];
  #else
    SerNum = HW_OCOTP_MAC0 & 0xFFFFFF;
  #endif

  FsDateTime::callback = dateTime;

  for(int ii=0; ii<5;ii++)
  {
    if (sd->begin(SD_CONFIG)) 
    { Serial.println("card initialized.");
      haveStore=1;
      return 1;
    }
    else
    { Serial.println("still trying..."); 
      delay(100);
    }
  }

  Serial.println("Card failed, or not present");
  // don't do anything more:
  return 0;
}

char * timeStamp(void)
{
  static char date_time[80];
  
  datetime_t t;
  rtc_get_datetime(&t);

  sprintf(date_time,"%04d%02d%02d_%02d%02d%02d",t.year,t.month,t.day,t.hour,t.min,t.sec);
  return date_time;
}

char * headerInit(int32_t fsamp, int32_t nchan, int32_t nbits, int serNum)
{
  int nbytes=nbits/8;

  memcpy(wav_hdr.rId,"RIFF",4);
  memcpy(wav_hdr.wId,"WAVE",4);
  memcpy(wav_hdr.fId,"fmt ",4);
  memcpy(wav_hdr.dId,"data",4);
  memcpy(wav_hdr.iId,"info",4);

  wav_hdr.rLen = 512-2*4; // will be updated at closing
  wav_hdr.fLen = 0x10;
  wav_hdr.iLen = 512 - 13*4;
  wav_hdr.dLen = 0; // will be updated at closing

  wav_hdr.nFormatTag=1;
  wav_hdr.nChannels=nchan;
  wav_hdr.nSamplesPerSec=fsamp;
  wav_hdr.nAvgBytesPerSec=fsamp*nbytes*nchan;
  wav_hdr.nBlockAlign=nchan*nbytes;
  wav_hdr.nBitsPerSamples=nbits;

  uint32_t tt=millis();
  strcpy(&wav_hdr.info[0], MAGIC);
  strcpy(&wav_hdr.info[4], timeStamp());
  sprintf(&wav_hdr.info[20]," %8x",serNum);
  memcpy(&wav_hdr.info[32], &tt,4);
  memcpy(&wav_hdr.info[36], getStore(),16*2);
  strcpy(&wav_hdr.info[68]," end");

  return (char *)&wav_hdr;
}

char * headerUpdate(int32_t nbytes)
{
  wav_hdr.dLen = nbytes;
  wav_hdr.rLen += wav_hdr.dLen;
  return (char *)&wav_hdr;
}

void writeHeader(char * wav_hdr) 
{ 
  uint64_t fpos;
  fpos = file.curPosition();
  Serial.printf("\n fpos=%d ",fpos);
  file.seekSet(0);
  file.write((const uint8_t*)wav_hdr,512);
  file.seekSet(fpos);
}

/*
char * wavHeader(uint32_t fileSize, int32_t fsamp, int32_t nchan, int32_t nbits)
{
//  int fsamp=48000;
//  int fsamp = fs;
//  int nchan=nch;

//  int nbits=16;
  int nbytes=nbits/8;

  int nsamp=fileSize/(nbytes*nchan);
  //
//  static char wheader[44];
  static char wheader[512];
  //
  strcpy(wheader,"RIFF");
  strcpy(wheader+8,"WAVE");
  strcpy(wheader+12,"fmt ");
  strcpy(wheader+36,"data");
  *(int32_t*)(wheader+16)= 16;// chunk_size
  *(int16_t*)(wheader+20)= 1; // PCM 
  *(int16_t*)(wheader+22)=nchan;// numChannels 
  *(int32_t*)(wheader+24)= fsamp; // sample rate 
  *(int32_t*)(wheader+28)= fsamp*nbytes; // byte rate
  *(int16_t*)(wheader+32)=nchan*nbytes; // block align
  *(int16_t*)(wheader+34)=nbits; // bits per sample 
  *(int32_t*)(wheader+40)=nsamp*nchan*nbytes; 
  *(int32_t*)(wheader+4)=36+nsamp*nchan*nbytes; 

   return wheader;
}
*/

int16_t makeHeader(int32_t *header)
{
    /**
     * @brief Make file header
     * @param header is pointer to header
     * 
     */
    datetime_t t;
    rtc_get_datetime(&t);

    sprintf((char *)header,"%s%04d%02d%02d_%02d%02d%02d",
            MAGIC,t.year,t.month,t.day,t.hour,t.min,t.sec);

    header[5] = 20;          // SW version
    header[6] = SerNum;      // serial number
    header[7] = fsamp;
    header[8] = NCHAN_ACQ;
    header[9] = t_acq;
    header[10] = t_on;
    header[11] = t_rep;
    header[12] = proc;
    header[13] = shift;
    header[14] = again;
    header[15] = dgain;
    header[16] = millis();

    header[127]=0x55555555;
    return 1;
}

int16_t checkEndOfFile(int16_t state)
{ 
  static uint32_t dta=0;

  if(state==RUNNING)
  {
    uint32_t tt = rtc_get();
    //
    uint32_t dt1 = tt % t_acq;
    if(dt1<dta) state = DOCLOSE;  	  	// should close file and continue
    dta = dt1;
    //
    if(state == DOCLOSE)                // in case of DOCLOSE
    if(t_rep>t_on)                      // and if foreseen  check for hibernation
    {
      uint32_t dt2 = (tt % t_rep);
      if(dt2>=t_on) state=DOHIBERNATE;  // should close file and hibernate
    }
  }
  return state;

}

uint16_t checkDiskSpace(void)
{   if((diskSize>0) && ((diskSpace=sd->freeClusterCount()) > MIN_SPACE)) return 1;
    return 0;
}

uint16_t newFolder(int d)
{ static int d0=-1;
  if(d==d0) return 0; // same day
  d0=d;
  return 1;
}

int16_t newDirectory(char *dirName)
{   
    datetime_t t;
    rtc_get_datetime(&t);

    if(HourDir)
    {
      if(newFolder(t.hour))
      {   
          sprintf(dirName, "/%s%06x_%04d%02d%02d/%02d/", 
              DirPrefix,(unsigned int)SerNum, t.year,t.month,t.day,t.hour);
          //
          Serial.println(); Serial.print(": "); Serial.print(dirName); 
          return 1;   // have new directory
      }
    }
    else
    {
      if(newFolder(t.day))
      {   
          sprintf(dirName, "/%s%06x_%04d%02d%02d/", 
              DirPrefix,(unsigned int)SerNum, t.year,t.month,t.day);
          //
          Serial.println(); Serial.print(": "); Serial.print(dirName);
          return 1;   // have new directory
      }
    }
    return 0;       // keep old directory

}

int16_t newFileName(char *fileName)
{
    datetime_t t;
    rtc_get_datetime(&t);
    if(proc==0)
      sprintf(fileName, "%s%04d%02d%02d_%02d%02d%02d.wav", FilePrefix, t.year,t.month,t.day,t.hour,t.min,t.sec);
    else
      sprintf(fileName, "%s%04d%02d%02d_%02d%02d%02d.bin", FilePrefix, t.year,t.month,t.day,t.hour,t.min,t.sec);
    //
    Serial.println(); Serial.print(": "); Serial.print(fileName);
    Serial.print(" "); Serial.print(micros());

    return 1;
}

static char dirName[80];
static char fileName[80];
static int32_t fileHeader[128];
uint32_t nbuf;
void do_hibernate(uint32_t dt);

/**************** main data filing routine ************************/
int16_t storeData(int16_t status)
{
    if(status==CLOSED) // file closed: should open
    {   //if(!checkDiskSpace()) {return STOPPED;}
        //
        if(newDirectory(dirName))
        {   if(!sd->exists(dirName) && !sd->mkdir(dirName)) {Serial.println("Error mkdir"); return STOPPED;}         
            if(!sd->chdir(dirName)) {Serial.println("Error chdir"); return STOPPED;}
        }
        
        if(newFileName(fileName))
        {   
            file = sd->open(fileName, FILE_WRITE); 
            if(file) 
            { status = OPENED; 
            }
            else 
            {   Serial.println("Failing open file");
                return STOPPED; 
            }
        } 
        else
        {  return STOPPED; // if file open fails: don't do anything
        }
    }
    //
    if(status==OPENED) // file is open: write first record (header)
    {   nbuf=0;
        char *hdr=0;
        if(proc==0)
        { 
          hdr = headerInit(fsamp, NCHAN_ACQ, NBITS, SerNum);
        }  
        else
        {
          makeHeader(fileHeader);
          hdr=(char *)fileHeader;
        }
        int nd;
        if((nd=file.write((const uint8_t*)hdr,512)) < 512) 
        { status = DOCLOSE;
        }
        else status=RUNNING;
    }
    //
    if(status==RUNNING) // file is open and header written: store data records
    {   uint32_t nd;
        if((nd=file.write((const uint8_t*)diskBuffer,4*MAX_DISK_BUFFER)) < 4*MAX_DISK_BUFFER) 
        { Serial.print(">"); 
          Serial.print(nd); 
          Serial.print(" "); 
          Serial.println(status); 
          status=DOCLOSE; 
        }
        else
          nbuf++;
        //
        disk_count++;
        if((nbuf % 1000)==0 ) file.flush();
    }    

    // following is done independent of data availability
    if((status==DOCLOSE) || (status==DOHIBERNATE) || (status==MUSTSTOP)) // should close file or stop acquisition
    {   if(file)
        {   if(proc==0)
            {
              char *hdr = headerUpdate(nbuf*MAX_DISK_BUFFER*4);
              writeHeader(hdr);
            }
            file.close();
        }

        if(status==DOHIBERNATE)
        { if( t_rep > t_on) 
          {
            // shutdown acq board
            adcReset();
            acqPower(LOW);
            do_hibernate(t_rep);
          }
          else
          {
            status = CLOSED;   // do not hibernate
          }
        }
        else if(status==DOCLOSE)
        {
          status=CLOSED;
        }
        else if(status==MUSTSTOP)
        {
          status=STOPPED;
          digitalWriteFast(13,LOW);
        }
    }
    return status;
}
//
volatile int32_t logBuffer[8];
int16_t saveData(int16_t status)
{
    if(status==STOPPED) 
    { while(queue_isBusy()) {;} //wait if acq writes to queue
      pullData((uint32_t*)tempBuffer0);
      for(int ii=0;ii<8;ii++) logBuffer[ii]=tempBuffer0[ii];
    }

    if(status<CLOSED) return status; // we are stopped: don't do anything

    status=checkEndOfFile(status);

    if(getDataCount() >= NDBL)
    { 
      if(proc==0)
      { 
        for(int ii=0; ii<NDBL; ii++)
        { while(queue_isBusy()){continue;} //wait if acq writes to queue
          pullData((uint32_t *)&tempBuffer0[ii*NBUF_ACQ]);
        }

        for(int ii=0;ii<8;ii++) logBuffer[ii]=tempBuffer0[ii];

        if(NBITS==32)
        {// wav mode; store original 32 bits
          for(int ii=0; ii<MAX_TEMP_BUFFER;ii++) 
          { diskBuffer[ii]=tempBuffer0[ii];
          }
        }
        else if(NBITS==24)
        { // wav mode; store only top 24 bits

          int jj=0;
          uint32_t * inpp=(uint32_t *) tempBuffer0;
          uint8_t * outptr=(uint8_t *) diskBuffer;
          for(int ii=0; ii<MAX_TEMP_BUFFER;ii++)
          {
            outptr[jj++]=(inpp[ii]) &0xff;
            outptr[jj++]=(inpp[ii]>>8) &0xff;
            outptr[jj++]=(inpp[ii]>>16) &0xff;
          }
        }
        else if(NBITS==16)
        { // wav mode; store only top 16 bits
          uint32_t * inpp=(uint32_t *) tempBuffer0;
          uint16_t * outptr=(uint16_t *) diskBuffer;
          for(int ii=0; ii<MAX_TEMP_BUFFER;ii++)
          {
            outptr[ii]=(inpp[ii]>>16);
          }
        }
      }
      else
      { // compressed mode; store all 32 bits
        for(int ii=0; ii<NDBL; ii++)
        { while(queue_isBusy()){continue;} //wait if acq writes to queue
          pullData((uint32_t *)&diskBuffer[ii*NBUF_ACQ]);
        }
        for(int ii=0;ii<8;ii++) logBuffer[ii]=diskBuffer[ii];
      }
      if(haveStore)
      {
        status=storeData(status);
      }
    }

    return status;
}

#if defined(TARGET_RP2040)
uint32_t getAlarmTime(uint32_t secs) {return 0;}

void powerDown(void) {}

void do_hibernate(uint32_t t_rep) {}

void reboot(void);
#else
/*********************** hibernate ******************************/
#include "core_pins.h"
/*  hibernating is shutting down the power snvs mode
    only RTC continuoes to run (if there is a 3V battery or power)
    hipernation is controlled by t_rep (sec), t_1,t_2,t_3, t_4 (h)

    for t_rep > t_on,  system will hibernate until next multiple of t_tep 

    t_1 to t_4 describe 2 acquirition windows (unit hour)
    acquisition happens from t_1 to t_2 and t_3 to t_4 
    (0 <= t_1 <= t_2 <= t_3 <=t_4 <=24)
    24 hour aquisition is ensured by t_1=0, t_2=12, t_3=12, t_4=24

    wakeup time is estimated by getAlarmTime
*/
uint32_t getAlarmTime(uint32_t secs)
{   // estimate the wakup-time in seconds 
    // input: actual time
    // output: next wakup time
    //
    uint32_t dd = secs/(24*3600); // days
    uint32_t hh =(secs%(24*3600))/3600; // hour into day

    if(((hh>=t_1) && (hh<t_2)) || ((hh>=t_3) && (hh<t_4)) )
    {
      secs = ((secs/t_rep)+1)*t_rep;
    }
    else if (hh<t_1)  // from mid-night to t_1
    {
      secs = dd*(24*3600) + t_1*3600;
    }
    else if (hh>=t_2) // between the two recording periods
    {
      secs = dd*(24*3600) + t_3*3600;
    }
    else if (hh>=t_4) // after the second recording period (goes into next day)
    {
      secs = dd*(24*3600) + (t_1+24-t_4)*3600;

    }
    return secs;
}

void powerDown(void)
{
  SNVS_LPCR |= (1 << 6); // turn off power
  while (1) asm("wfi");      
}

#define SNVS_LPCR_LPTA_EN_MASK          (0x2U)

void do_hibernate(uint32_t t_rep)
{
    uint32_t tmp = SNVS_LPCR;   // save control register

    SNVS_LPSR |= 1;
    asm volatile("DSB");

    // disable alarm
    SNVS_LPCR &= ~SNVS_LPCR_LPTA_EN_MASK;
    while (SNVS_LPCR & SNVS_LPCR_LPTA_EN_MASK);

    // clear alarm value
    SNVS_LPTAR = 0;
    while (SNVS_LPTAR != 0);

    __disable_irq();

    //get Time:
    uint32_t lsb, msb;
    do {
      msb = SNVS_LPSRTCMR;
      lsb = SNVS_LPSRTCLR;
    } while ( (SNVS_LPSRTCLR != lsb) | (SNVS_LPSRTCMR != msb) );
    uint32_t secs = (msb << 17) | (lsb >> 15);

    //set alarm
    Serial.print(secs); Serial.print(" ");
    secs = getAlarmTime(secs);
    Serial.println(secs);

    SNVS_LPTAR = secs;
    while (SNVS_LPTAR != secs);

    // restore control register and set alarm
    SNVS_LPCR = tmp | SNVS_LPCR_LPTA_EN_MASK; 
    while (!(SNVS_LPCR & SNVS_LPCR_LPTA_EN_MASK));

    __enable_irq();
  
    //
    powerDown(); 
}
#endif
