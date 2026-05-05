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

#include <SdFat.h>

#include "mConfig.h"
#include "mQueue.h"
#include "mAcq.h"
#include "mRTC.h"
#include "mFiling.h"

volatile uint32_t t_acq=T_ACQ;
volatile uint32_t t_on=T_ON;
volatile uint32_t t_off=T_OFF;

#if SDFAT_FILE_TYPE != 3
 #error "SDFAT_FILE_TYPE != 3: edit SdFatConfig.h \
 (e.g. C:\Users\...\AppData\Local\Arduino15\packages\rp2040\hardware\rp2040\3.9.3\libraries\ESP8266SdFat\src") 

#endif

#if defined(TARGET_RP2040)
  const int _MOSI = SPI_MOSI;
  const int _MISO = SPI_MISO;
  const int _CS   = SPI_CS;
  const int _SCK  = SPI_SCK;

  #if ENABLE_DEDICATED_SPI !=1
  #error "ENABLE_DEDICATED_SPI !=1: edit SdFatConfig.h"
  #endif

  // Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
  #define SD_CONFIG SdSpiConfig(_CS, DEDICATED_SPI, SD_SCK_MHZ(25))

  extern "C" void flash_get_unique_id(uint8_t *p);
  uint32_t UniqueID[2];

#elif defined(__IMXRT1062__) && defined(ARDUINO_TEENSY_MICROMOD)
  #define SD_CONFIG SdioConfig(FIFO_SDIO)

#elif defined(__IMXRT1062__) && defined(ARDUINO_TEENSY41)
  #define SD_CONFIG SdioConfig(FIFO_SDIO)
#endif

SdFs sd;
FsFile file;

static HdrStruct wav_hdr;

volatile int haveStore=0;
uint32_t diskSpace=0;
uint32_t diskSize=0;

// define the number of audio blocks to load for writing to disk
// data are acquired always with 32 bit
#if NBITS_WAV==32
  const int nblocks=NDBL;
#elif NBITS_WAV==24
  const int nblocks=4*NDBL/3;
#elif NBITS_WAV==16
  const int nblocks=2*NDBL;
#endif
    

#define MAX_TEMP_BUFFER (nblocks*NBUF_ACQ)
static int32_t tempBuffer0[MAX_TEMP_BUFFER];
static int32_t tempBuffer1[MAX_TEMP_BUFFER];

#define MAX_DISK_BUFFER (NDBL*NBUF_ACQ)
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

    digitalWrite(_CS,HIGH); // unselect
    SPI.transfer16(0xFF);  // Insure MISO goes to low Z.
    
    flash_get_unique_id((uint8_t *) UniqueID);
    SerNum=UniqueID[1];
  #else
    SerNum = HW_OCOTP_MAC0 & 0xFFFFFF;
  #endif

  FsDateTime::callback = dateTime;
  
  for(int ii=0; ii<5;ii++)
  { Serial.println("sd-config");
    if (sd.begin(SD_CONFIG)) 
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

  #define MAGIC "WMXZ"

  strcpy(wav_hdr.rId,"RIFF");
  strcpy(wav_hdr.wId,"WAVE");
  strcpy(wav_hdr.fId,"fmt ");
  strcpy(wav_hdr.dId,"data");
  strcpy(wav_hdr.iId,"info");

  wav_hdr.rLen=512-2*4; // will be updated at closing
  wav_hdr.fLen=0x10;
  wav_hdr.iLen = 512 - 13*4;
  wav_hdr.dLen = 0; // will be updated at closing

  wav_hdr.nFormatTag=1;
  wav_hdr.nChannels=1;
  wav_hdr.nSamplesPerSec=fsamp;
  wav_hdr.nAvgBytesPerSec=fsamp*nbytes*nchan;
  wav_hdr.nBlockAlign=nchan*nbytes;
  wav_hdr.nBitsPerSamples=nbits;

  strcpy(&wav_hdr.info[0], MAGIC);
  strcpy(&wav_hdr.info[4], timeStamp());
  sprintf(&wav_hdr.info[20]," %8x",serNum);
  strcpy(&wav_hdr.info[30]," end");

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
  uint32_t fpos;
  fpos = file.curPosition();
  Serial.printf(" fpos=%d ",fpos);
  file.seek(0);
  file.write((const uint8_t*)wav_hdr,512);
  file.seek(fpos);
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
    #define MAGIC "WMXZ"
    datetime_t t;
    rtc_get_datetime(&t);

    sprintf((char *)header,"%s%04d%02d%02d_%02d%02d%02d",
            MAGIC,t.year,t.month,t.day,t.hour,t.min,t.sec);

    header[5] = 20;          // SW version
    header[6] = SerNum;      // serial number
    header[7] = fsamp;
    header[8] = NCH;
    header[9] = t_acq;
    header[10] = t_on;
    header[11] = t_off;
    header[12] = proc;
    header[13] = shift;

    header[127]=0x55555555;
    return 1;
}

int16_t checkEndOfFile(int16_t status)
{ static volatile uint32_t tx_=0;
  uint32_t tx;
  tx=rtc_get();
  tx = tx % t_acq;
  if((status>OPENED) && (tx_>0) && (tx < tx_)) status=DOCLOSE;
  tx_=tx;
  return status;  
}

uint16_t checkDiskSpace(void)
{   if((diskSize>0) && ((diskSpace=sd.freeClusterCount()) > MIN_SPACE)) return 1;
    return 0;
}

uint16_t newFolder(int h)
{ static int ho=-1;
  if(h==ho) return 0;
  ho=h;
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
      sprintf(fileName, "%s%04d_%02d_%02d_%02d%02d%02d.wav", FilePrefix, t.year,t.month,t.day,t.hour,t.min,t.sec);
    else
      sprintf(fileName, "%s_%02d%02d%02d.bin", FilePrefix, t.hour,t.min,t.sec);
    //
    Serial.println(); Serial.print(": "); Serial.print(fileName);
    return 1;
}

static char dirName[80];
static char fileName[80];
static int32_t fileHeader[128];
uint32_t nbuf;

/**************** main data filing routine ************************/
int16_t storeData(int16_t status)
{
    if(status==CLOSED) // file closed: should open
    {   //doTransactions(true);
        //if(!checkDiskSpace()) {return STOPPED;}
        //
        if(newDirectory(dirName))
        {   if(!sd.exists(dirName) && !sd.mkdir(dirName)) {Serial.println("Error mkdir"); return STOPPED;}         
            if(!sd.chdir(dirName)) {Serial.println("Error chdir"); return STOPPED;}
        }
        
        if(newFileName(fileName))
        {   
            file = sd.open(fileName, FILE_WRITE); 
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
          hdr = headerInit(fsamp, NCH, NBITS_WAV, SerNum);
        }  
        else
        {
          makeHeader(fileHeader);
          hdr=(char *)fileHeader;
        }
        if(file.write((const uint8_t*)hdr,512) < 512) 
        { status = DOCLOSE;
        } 
        else status=RUNNING;
    }
    //
    if(status==RUNNING) // file is open, header written: store data records
    {   uint32_t nd;
        if((nd=file.write((const uint8_t*)diskBuffer,4*MAX_DISK_BUFFER)) < 4*MAX_DISK_BUFFER) 
        { Serial.print(">"); Serial.print(nd); Serial.print(" "); Serial.println(status); status=DOCLOSE; }
        else
          nbuf++;
        disk_count++;
        if((nbuf % 1000)==0 ) file.flush();
    }    

    // following is done independent of data availability
    if((status==DOCLOSE) || (status==MUSTSTOP)) // should close file or stop acquisition
    {   if(file)
        {   if(proc==0)
            {
              char *hdr = headerUpdate(nbuf*MAX_DISK_BUFFER*4);
              writeHeader(hdr);

            }
            file.close();
        }
        status= (status==DOCLOSE)? CLOSED : STOPPED;
    }
    return status;
}
//
volatile int32_t logBuffer[8];
int16_t saveData(int16_t status)
{
    if(status==STOPPED) 
    { 
      while(queue_isBusy()); //wait if acq writes to queue
      pullData((uint32_t*)tempBuffer1);
      for(int ii=0;ii<8;ii++) logBuffer[ii]=tempBuffer1[ii];
    }

    if(status<CLOSED) return status; // we are stopped: don't do anything

    status=checkEndOfFile(status);

    if(getDataCount()>=nblocks)
    { 
      if(proc==0)
      { 
        for(int ii=0; ii<nblocks; ii++)
        { while(queue_isBusy()); //wait if acq writes to queue
          pullData((uint32_t *)&tempBuffer0[ii*NBUF_ACQ]);
        }

        // differentiate
        static int32_t data0=0;
        tempBuffer1[0] =tempBuffer0[0]-data0;
        for(int ii=1;ii<MAX_TEMP_BUFFER; ii++) tempBuffer1[ii]=tempBuffer0[ii]-tempBuffer0[ii-1];
        data0=tempBuffer0[MAX_TEMP_BUFFER-1];

        // remove initial offset
        static int first=1; if(first)  { tempBuffer1[0]=0; first=0;} 

        // integrate
        static int32_t data1=0;
        tempBuffer1[0]=tempBuffer1[0]+data1;
        for(int ii=1;ii<MAX_TEMP_BUFFER; ii++) tempBuffer1[ii] += tempBuffer1[ii-1];
        data1=tempBuffer1[MAX_TEMP_BUFFER-1];        

        for(int ii=0;ii<8;ii++) logBuffer[ii]=tempBuffer1[ii];

        if(NBITS_WAV==32)
        {// wav mode; store original 32 bits
          for(int ii=0; ii<MAX_TEMP_BUFFER;ii++) 
          { diskBuffer[ii]=tempBuffer1[ii];
          }
        }
        else if(NBITS_WAV==24)
        { // wav mode; store only top 24 bits

          int jj=0;
          uint8_t * outptr=(uint8_t *) diskBuffer;
          uint32_t *inpp=(uint32_t *) tempBuffer1;
          for(int ii=0; ii<MAX_TEMP_BUFFER;ii++)
          {
            outptr[jj++]=(inpp[ii]>>8) &0xff;
            outptr[jj++]=(inpp[ii]>>16) &0xff;
            outptr[jj++]=(inpp[ii]>>24) &0xff;
          }
        }
        else if(NBITS_WAV==16)
        { // wav mode; store only bottom 16 bits
          int jj=0;
          uint16_t * outptr=(uint16_t *) diskBuffer;
          uint32_t *inpp=(uint32_t *) tempBuffer1;
          for(int ii=0; ii<MAX_TEMP_BUFFER;ii++)
          {
            outptr[jj++]=(inpp[ii]>>8) &0xffff; 
          }
        }
      }
      else
      { // compressed mode; store all 32 bits
        for(int ii=0; ii<NDBL; ii++)
        { while(queue_isBusy()); //wait if acq writes to queue
          pullData((uint32_t *)&diskBuffer[ii*NBUF_ACQ]);
        }
        for(int ii=0;ii<8;ii++) logBuffer[ii]=diskBuffer[ii];
      }
      if(haveStore)
      { digitalWrite(LED_BUILTIN,HIGH);
        status=storeData(status);
        digitalWrite(LED_BUILTIN,LOW);
      }
    }

    return status;
}
