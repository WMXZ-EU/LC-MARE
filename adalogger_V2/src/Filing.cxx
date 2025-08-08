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
#include "SdFat.h"

#include "global.h"
#include "rp2040.h"
#include "mRTC.h"
#include "Filing.h"
#include "Adc.h"

uint32_t t_acq = T_ACQ;   // seconds
uint32_t t_on  = T_ON;    // minutes
uint32_t t_rep = T_REP;   // minutes (for continuous recording set t_rep < t_acq)

char ISRC[40]={' '}; //  Source
char ICMS[40]={' '}; //  Organization
char IART[40]={' '}; // 'Artist' (creator)
char IPRD[40]={' '}; // 'Product' (Activity)
char ISBJ[40]={' '}; // 'subject' (Area)
char INAM[40]={' '}; // 'Name' (location id)

// microSD card
#if SDFAT_FILE_TYPE != 3
 #error "SDFAT_FILE_TYPE != 3: edit SdFatConfig.h"
#endif

// definitions
#if defined(ARDUINO_ADAFRUIT_FEATHER_RP2040_ADALOGGER)
  // for SPI
  #define SPI_SCK   18
  #define SPI_MOSI  19
  #define SPI_MISO  20
  #define SPI_CS    23

  static uint16_t have_sd =0;
  void spi_init()
  { pinMode(SPI_CS, OUTPUT);
    digitalWrite(SPI_CS,HIGH);
    //
    SPI1.setCS(SPI_CS);
    SPI1.setRX(SPI_MISO);
    SPI1.setTX(SPI_MOSI);
    SPI1.setSCK(SPI_SCK);
  }
  // Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
  #define SD_CONFIG SdSpiConfig(SPI_CS, SHARED_SPI, SD_SCK_MHZ(50),(SpiPort_t *) &SPI1)
#endif

SdFs sd;  // defined in storage_configure
FsFile file;

// Call back for file timestamps.  Only called for file create and sync(). needed by SDFat
void dateTime(uint16_t* date, uint16_t* time, uint8_t* ms10) 
{
    datetime_t t;
    rtcGetDatetime(&t);    

    *date = FS_DATE(t.year,t.month,t.day);
    *time = FS_TIME(t.hour,t.min,t.sec);
    *ms10 = 0;
}
// wav file header
static HdrStruct wav_hdr;
char *wav_Info_ptr=wav_hdr.info;

char * insertChunk(char *ptr, const char * id, char * txt)
{
  memcpy(ptr,id,4); ptr+=4;
    int leno=strlen(txt);
    int len = (1+(leno+1)/4)*4;
    *(uint32_t *) ptr = len; ptr+=4;
    memcpy(ptr,txt,leno); ptr+=leno; 
    for(int ii=leno; ii<len;ii++) *ptr++=0;
    return ptr;
}

void wavInfoInit(void)
{
  char *wptr=wav_hdr.info;
  char txt[40];
  sprintf(txt,"%s:%s",Program,Version);
  wptr=insertChunk(wptr,"ISFT",txt);
  wptr=insertChunk(wptr,"IGNR",(char*)"PAM");
  wptr=insertChunk(wptr,"ISRC",ISRC);
  wptr=insertChunk(wptr,"ICMS",ICMS);
  wptr=insertChunk(wptr,"IART",IART);
  wptr=insertChunk(wptr,"IPRD",IPRD);
  wptr=insertChunk(wptr,"ISBJ",ISBJ);
  wptr=insertChunk(wptr,"INAM",INAM);
  Serial.println("info initalized");
  wav_Info_ptr=wptr;
}

void wavHeaderInit(int32_t fsamp, int32_t nchan, int32_t nbits)
{
  int nbytes=nbits/8;

  memcpy(wav_hdr.rId,"RIFF",4);
  memcpy(wav_hdr.wId,"WAVE",4);
  memcpy(wav_hdr.fId,"fmt ",4);
  memcpy(wav_hdr.dId,"data",4);
  memcpy(wav_hdr.lId,"LIST",4);
  memcpy(wav_hdr.iId,"INFO",4);

  wav_hdr.fLen = 16;
  wav_hdr.lLen = 512 - 13*4;  // length of list chunk

  wav_hdr.rLen = 512-2*4;     // will be updated at closing
  wav_hdr.dLen = 0;           // will be updated at closing
  
  wav_hdr.nFormatTag=1;
  wav_hdr.nChannels=nchan;
  wav_hdr.nSamplesPerSec=fsamp;
  wav_hdr.nAvgBytesPerSec=fsamp*nbytes*nchan;
  wav_hdr.nBlockAlign=nchan*nbytes;
  wav_hdr.nBitsPerSamples=nbits;
  //
  wavInfoInit();
}

char datestring[80];
char infotext[80];
char * wavHeaderUpdate(int32_t nbytes, int16_t vsens)
{
  char *wptr=wav_Info_ptr;
  wptr=insertChunk(wptr,"ICRD",datestring);
  //
  sprintf(infotext,"%6d; %6d; %6d; %6d; %6d; %6d.",t_acq,t_on,t_rep,fsamp/1000,again, vsens);
  wptr=insertChunk(wptr,"IKEY",infotext);
  //
  if(missed_acq>0)
  {   uint32_t * ptr=get_missed_list(); 
      char *istr=infotext;
      for(int ii=0; ii<missed_acq && ii<32; ii++) {sprintf(istr,"%4d",ptr[ii%32]); istr+=4;} 
      wptr=insertChunk(wptr,"ICMT",infotext);
  }
  else
  {   for(int ii=0; ii<8+32*4; ii++) wptr[ii]=0; 
  }
  
  wav_hdr.dLen = nbytes;
  wav_hdr.rLen = nbytes+512-2*4;
  return (char *)&wav_hdr;
}

uint16_t SD_init(void)
{
  getUID();
  //
  spi_init();
  int jj;
  for(jj=0;jj<5;jj++) if (sd.begin(SD_CONFIG)) break; else delay(1000);
  if(jj==5)
  {
    Serial.printf("SD Storage %d failed or missing",SPI_CS);  Serial.println();
    return 0;
  }
  else
  {
    uint64_t totalSize = sd.clusterCount();
    uint64_t freeSize  = sd.freeClusterCount();
    uint32_t clusterSize = sd.bytesPerCluster();
    Serial.printf("Storage %d ",SPI_CS); 
    Serial.print("; total clusters: "); Serial.print(totalSize); 
    Serial.print(" free clusters: "); Serial.print(freeSize);
    Serial.print(" clustersize: "); Serial.print(clusterSize/1024); Serial.println(" kByte");

    FsDateTime::callback = dateTime;
    //
    // signal write acivity by flashing LED
    pinMode(LED_BUILTIN, OUTPUT);

    // prepare wav header (const content)
    //prep_header(NCH, FSAMP, MBIT);
    wavHeaderInit(FSAMP, NCH, MBIT);
    have_sd=1;
  }
  return 1;
}

void SD_stop(void)
{
  //https://github.com/greiman/SdFat/issues/401
  if(have_sd)
    sd.card()->syncDevice();
}

uint32_t mdt=0;
#if PROC==0
  // write to file
  int32_t storeData(int32_t *buffer)
  {
      uint32_t nbuf=NBUF_I2S*4;
      digitalWrite(LED_BUILTIN, HIGH);
      uint32_t to=millis();
      int ndat= file.write(buffer,nbuf);
      uint32_t dt=(millis()-to);
      if(dt>mdt) mdt=dt;
      digitalWrite(LED_BUILTIN, LOW);
      return ndat;
  }
  int32_t flash_disk(void) { return 0;}

#elif PROC==1
  //compress and write to file
  #define MBIT 32
  static int kko=0;
  static int32_t disk_buffer[NBUF_I2S];

  int32_t flushBuffer(int32_t nbuf)
  {
    digitalWrite(LED_BUILTIN, HIGH);
    uint32_t to=millis();
    int ndat= file.write(disk_buffer,nbuf);
    uint32_t dt=(millis()-to);
    if(dt>mdt) mdt=dt;
    digitalWrite(LED_BUILTIN, LOW);
    return ndat;
  }

  int32_t storeData(int32_t *buffer)
  {
    int32_t ndat=0;
    for(int ii=0;ii<NBUF_I2S;ii++) buffer[ii]=buffer[ii]>>8;
    uint32_t amax=0;
    for(int ii=0;ii<NBUF_I2S;ii++) 
    { int32_t tmp;
      tmp=buffer[ii];
      if(tmp<0) tmp=-tmp;
      if(tmp>amax) amax=tmp;
    }
      // estimate mask (allow only values > 2)
    uint32_t nb=0;
    for(nb=2; nb<24; nb++) if(amax < (1<<(nb-1))) break;

    uint32_t ncmp = (NBUF_I2S*nb) / MBIT;
    uint32_t mask = (1<<nb) -1;

    // pack data
    // 
    uint32_t *tempData = (uint32_t *) buffer;
    uint32_t *outData  = (uint32_t *) disk_buffer;
    for(int ii=kko;ii<NBUF_I2S;ii++) outData[ii]=0;

    int kk = kko;
//    if(kk==NBUF_I2S-2)
//    { uint32_t nbuf=NBUF_I2S*4;
//      ndat += flushBuffer(nbuf);
//      kk=0;
//    }
    outData[kk++]=nb;
    outData[kk++]=ncmp;
    //
    int nx = MBIT;
    for (int ii = 0; ii < NBUF_I2S; ii ++)
    {   nx -= nb;
        uint32_t tmp = tempData[ii] & mask;
        if(nx > 0)
        {   outData[kk] |= (tmp << nx);
        }
        else if(nx==0) 
        {   outData[kk++] |= tmp;
            if(kk==NBUF_I2S)
            { uint32_t nbuf=NBUF_I2S*4;
              ndat += flushBuffer(nbuf);
              kk=0;
            }
            nx=MBIT;
        } 
        else    // nx is < 0
        {   outData[kk++] |= (tmp >> (-nx));
            if(kk==NBUF_I2S)
            { uint32_t nbuf=NBUF_I2S*4;
              ndat += flushBuffer(nbuf);
              kk=0;
            }
            nx += MBIT;
            outData[kk] = (tmp << nx);
        }
    }
    kko=kk;
/*
    uint32_t nbuf=(kk/128)*128*4;
    //Serial.printf("%d %d %d %d %08x %08x %08x %08x\n",nb,ncmp,kk,kko,
    //  disk_buffer[kko+0],disk_buffer[kko+1],disk_buffer[kko+2],disk_buffer[kko+3]);
    ndat += flushBuffer(nbuf);
    //
    kko=kk % 128;
    kk=(kk/128)*128;
    for(int ii=0;ii<kko;ii++) disk_buffer[ii]=disk_buffer[kk++];
*/
    return ndat;
  }

  int32_t flash_disk(void)
  {
    uint32_t nbuf=kko*4;
    kko=0;
    return flushBuffer(nbuf);
  }

#endif

// Filing
uint32_t num_bytes_written=0;
char date_str[20];
char time_str[20];
uint32_t old_day=32;
uint32_t old_hour=24;
uint32_t old_time = 0;

extern uint32_t loop_count;
extern uint32_t data_count;

char dayDir[40];
char hourDir[10];

status_t logger(int32_t * buffer,status_t status)
{
  if(status==CLOSED)
  { // open new file
    neo_pixel_show(10, 10, 10);

    datetime_t t;
    rtcGetDatetime(&t);
    //
    sprintf(date_str,"%04d%02d%02d",t.year,t.month,t.day);
    sprintf(time_str,"%02d%02d%02d",t.hour,t.min,t.sec);
    sprintf(datestring,"%s_%s",date_str,time_str);          // used in wav header
    //
    if(t.day != old_day)
    { 
      sprintf(dayDir,"/%s_%s",uid_str,date_str);
      if(!sd.exists(dayDir))
      { sd.mkdir(dayDir);
      }
      sd.chdir(dayDir);
      old_day=t.day;
    }
    //
    if(t.hour != old_hour)
    { 
      sd.chdir(dayDir);

      sprintf(hourDir,"%02d",t.hour);
      if(!sd.exists(hourDir))
      { sd.mkdir(hourDir);
      }
      sd.chdir(hourDir);        
      old_hour = t.hour;
    }
    //
    char fileName[80];
    sprintf(fileName,"%s_%s_%s.%s",uid_str,date_str,time_str,"wav");
    file=sd.open(fileName, FILE_WRITE);
    if(!file)
    { status=JUST_STOPPED; 
      neo_pixel_show(0, 0, 10);
      return status;
    }
    //
    Serial.print(fileName); Serial.print("; ");
    file.write(&wav_hdr,512);
    num_bytes_written=0;
    status=RECORDING;
    neo_pixel_show(0, 0, 0);
  }
  //
  if((status==RECORDING) || (status==MUST_STOP))
  { // write to disk
    num_bytes_written += storeData(buffer);
    
    // check to close file
    uint32_t tt = rtc_get();
    uint32_t tmp_time=(tt % t_acq );
    if((tmp_time < old_time) || (status == MUST_STOP))
    {
      // flash last buffer
      num_bytes_written += flash_disk();

      int16_t vsens=analogRead(A1);
      // create header for WAV file and write to SD card
      char *wav_header=wavHeaderUpdate(num_bytes_written,vsens);
  	  //
      uint64_t fpos;
      fpos = file.curPosition();
      //Serial.printf(" fpos=%d ",fpos);
      file.seekSet(0);
      file.write((const uint8_t*)wav_header,512);
      file.seekSet(fpos);

      file.close();
      //
      uint32_t num_samples = num_bytes_written / (4 * NCH);
      if(Serial)
      { Serial.printf("\t%5d %8d %3d %2d %4d %6d\t%8x %8x %8x %8x\n", 
                        loop_count, num_samples, data_count, missed_acq, mdt, vsens,
                                            buffer[0],buffer[1],buffer[2],buffer[3]);
        if(missed_acq>0) 
        { uint32_t * ptr=get_missed_list(); 
          Serial.print("Missed "); Serial.print(missed_acq); Serial.print(": ");
          for(int ii=0; ii<missed_acq; ii++) {Serial.print(ptr[ii % 32]); Serial.print(' ');} Serial.println();
        }
      }
      data_count = 0;
      loop_count = 0;
      missed_acq = 0;
      mdt=0;
      reset_missed_list();
      //
      // check for stopping or hibernation
      if(status == MUST_STOP)
      { Serial.print(" stopped ");
        status = JUST_STOPPED;
      }
      else
      {
        status = CLOSED;
        //
        if(t_rep>t_on)                      // if foreseen  check for hibernation
        { uint32_t ttm=tt/60;
          //Serial.printf("%d %d %d %d\n",t_acq,t_rep,ttm,(ttm % t_rep));

          uint32_t dt2 = (ttm % t_rep);
          if(dt2>=t_on) 
          {
            uint32_t alarm=((ttm/t_rep)+1)*t_rep*60;
            hibernate_until(alarm);
          }
        }
      }
    }
    old_time = tmp_time;
  }
  return status;
}

/*************************Configuration file ****************************************/
void eepromWrite32(byte a, uint32_t v);
void eepromCommit();

static char configText[16*80]={0};  // maximal 16 lines of 80 characters each
static int configIndex[16]={0};     // maximal 16 parameters (actual 11 entries)
int16_t loadConfigfromFile(void)
{
  const int nmax=sizeof(configText);
    // load file into memmory
    int ii=0;
    FsFile file = sd.open("config.txt"); 
    if(file) 
    { while (file.available() && (ii<nmax)) 
      {
        configText[ii++]=file.read();
      }
      file.close(); 
    }
    else
      return 0;
    //
    // find menu entries
    int jj=0;
    for(int ii=0;ii<nmax;ii++) {if(configText[ii]=='!') configIndex[jj++]=ii;}
    // decode menu entries
    for(int ii=0;ii<jj;ii++){ 
      int i1=configIndex[ii]+1;
      int i2=i1;
      while(i2<nmax) {if((configText[i2]=='#')||(configText[i2]==';')) break; i2++;}
      configText[i2]=0;
      char *txt=&configText[configIndex[ii]];

      char *txt2=txt+2;
        switch(txt[1])
        {
          case 'a': sscanf(txt2,"%d",&t_acq); break;
          case 'o': sscanf(txt2,"%d",&t_on); break;
          case 'r': sscanf(txt2,"%d",&t_rep); break;
          case 'f': sscanf(txt2,"%d",&fsamp); fsamp *=1000; acqModifyFrequency(fsamp); break;
          case 'g': sscanf(txt2,"%d",&again); setAGain((int8_t)again&0xff); break;
          case 's': sscanf(txt2,"%s",&ISRC[0]); break; // source (AS1-200)
          case 'c': sscanf(txt2,"%s",&ICMS[0]); break; // commissioning organisation (WMXZ)
          case 'n': sscanf(txt2,"%s",&IART[0]); break; // name of operator (creator) (WMXZ)
          case 'p': sscanf(txt2,"%s",&IPRD[0]); break; // project (Development)
          case 'e': sscanf(txt2,"%s",&ISBJ[0]); break; // area (atHome)
          case 'l': sscanf(txt2,"%s",&INAM[0]); break; // location id (B01)
        }
    }
  return ii;
}

void configLoad(void)
{ 
  Serial.println(loadConfigfromFile());
}

void configShow(void)
{
  if(loadConfigfromFile()>0)
  {
    Serial.println("config Loaded");
    Serial.print("ISRC "); Serial.println(ISRC);
    Serial.print("ICMS "); Serial.println(ICMS);
    Serial.print("IART "); Serial.println(IART);
    Serial.print("IPRD "); Serial.println(IPRD);
    Serial.print("ISBJ "); Serial.println(ISBJ);
    Serial.print("INAM "); Serial.println(INAM);

    wavInfoInit();
  }
}
