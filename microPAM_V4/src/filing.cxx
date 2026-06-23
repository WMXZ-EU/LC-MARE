#include <SPI.h>
#include <SdFat.h>

#include "global.h"
#include "adc.h"
#include "rtc.h"
#include "filing.h"
#include "process.h"

#ifndef BUILTIN_SDCARD
  #define BUILTIN_SDCARD 254
#endif

extern uint32_t  acq_count;
extern uint32_t acq_missed;

uint16_t t_acq = T_ACQ;   // seconds
uint16_t t_on  = T_ON;    // minutes
uint16_t t_rep = T_REP;   // minutes (for continuous recording set t_rep < t_acq)

uint16_t h_rec[4] = {0,12,12,24};

char ISRC[40]={SRC_str}; //  Source
char ICMS[40]={CMS_str}; //  Organization
char IART[40]={ART_str}; // 'Artist' (creator)
char IPRD[40]={PRD_str}; // 'Product' (Activity)
char ISBJ[40]={SBJ_str}; // 'subject' (Area)
char INAM[40]={NAM_str}; // 'Name' (location id)
char startTime[40]={"2000-01-01 00:00:00"}; // Start Time

// definitions
static uint16_t have_sd =0;

//#if defined(ARDUINO_ADAFRUIT_FEATHER_RP2040_ADALOGGER)
#if defined (ARDUINO_ARCH_RP2040)
    #define _CS SD_CS
    #if MCU==RP_2040
      // Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
      #define SD_CONFIG SdSpiConfig(_CS, SHARED_SPI, SD_SCK_MHZ(48), (SpiPort_t *) &SPI1)
    #elif MCU==RP_2350
      // Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
      #define SD_CONFIG SdSpiConfig(_CS, SHARED_SPI, SD_SCK_MHZ(48))
    #endif

    void spi_init()
    { pinMode(_CS, OUTPUT);
      digitalWrite(_CS,HIGH);
      //
    }
#else
  #define _CS BUILTIN_SDCARD
  #define SD_CONFIG SdioConfig(FIFO_SDIO)

  void spi_init() {}
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
  typedef struct {
      char    rId[4];               //4
      unsigned int rLen;            //8
      char    wId[4];               //12
      char    fId[4];               //16
      unsigned int    fLen;           //20
      unsigned short nFormatTag;      //22
      unsigned short nChannels;       //24
      unsigned int nSamplesPerSec;    //28
      unsigned int nAvgBytesPerSec;   //32
      unsigned short nBlockAlign;     //36
      unsigned short  nBitsPerSamples;//38
      char    lId[4];                 //40
      unsigned int  lLen;             //44
      char    iId[4];                 //48
      char    info[512-14*4];         // fill header to 512 bytes (504=48+546)
      char    dId[4];                 //508
      unsigned int    dLen;           //512
  } HdrStruct;


static HdrStruct wav_hdr;
char *wav_Info_ptr=wav_hdr.info;

char * insertChunk(char *ptr, const char *id, char *txt)
{
  memcpy(ptr,id,4); ptr+=4;
    int leno=strlen(txt);
    int len = ((leno+3)/4)*4;
    *(uint32_t *) ptr = len; ptr+=4;
    memcpy(ptr,txt,leno); ptr+=leno; 
    for(int ii=leno; ii<len; ii++) *ptr++=0;
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
char infotext[256];

char * wavHeaderUpdate(int32_t nbytes, int16_t vsens)
{
  char *wptr=wav_Info_ptr;
  wptr=insertChunk(wptr,"ICRD",datestring);
  //
  sprintf(infotext,"%s; %4d; %4d; %4d; %4lu; %4lu; %6u %3d; %3d; %4d; %3d; %4d; %4d; %4d; %4d; %s.",
                    uid_strng,t_acq,t_on,t_rep,fsamp/1000,again, vsens,SHIFT,PROC_MODE, NBUF, NBUF_DISK/NBUF,
                    h_rec[0],h_rec[1],h_rec[2],h_rec[3],Version);
  wptr=insertChunk(wptr,"IKEY",infotext);
  //
  sprintf(infotext,"Version: %s; missed_acq: %lu",Version, acq_missed); 
  wptr=insertChunk(wptr,"ICMT",infotext);
  
  wav_hdr.dLen = nbytes;
  wav_hdr.rLen = nbytes+512-2*4;
  return (char *)&wav_hdr;
}

uint16_t SD_init(void)
{
  spi_init();
  int jj;
  for(jj=0;jj<5;jj++) if (sd.begin(SD_CONFIG)) break; else delay(1000);
  if(jj==5)
  {
    Serial.printf("SD Storage %d failed or missing",_CS);  Serial.println();
    return 0;
  }
  else
  {
    uint64_t totalSize = sd.clusterCount();
    uint64_t freeSize  = sd.freeClusterCount();
    uint32_t clusterSize = sd.bytesPerCluster();
    Serial.printf("Storage %d ",_CS); 
    Serial.print("; total clusters: "); Serial.print(totalSize); 
    Serial.print(" free clusters: "); Serial.print(freeSize);
    Serial.print(" clustersize: "); Serial.print(clusterSize/1024); Serial.println(" kByte");

    FsDateTime::callback = dateTime;
    //
    // signal write acivity by flashing LED
    pinMode(LED_BUILTIN, OUTPUT);

    // prepare wav header (const content)
    //prep_header(NCH, FSAMP, MBIT);
    wavHeaderInit(fsamp, NCHAN_ACQ, MBIT);
    have_sd=1;
  }
  return 1;
}

void storeConfigFile(void);
void SD_stop(void)
{ if(have_sd)
  {
    storeConfigToFile();
    //https://github.com/greiman/SdFat/issues/401
    sd.card()->syncDevice();  
  }
}

//---------------------------- Disk interface -------------------------------------------
uint32_t mdt=0;   // keep max write time
int write_disk(int32_t *buffer,int32_t nbuf)
{
    digitalWrite(LED_BUILTIN, HIGH);
    uint32_t to=millis();
    int ndat= file.write(buffer,nbuf);
    uint32_t dt=(millis()-to);
    if(dt>mdt) mdt=dt;
    digitalWrite(LED_BUILTIN, LOW);
    return ndat;
}

//---------------------------- Filing ----------------------------------
uint32_t num_bytes_written=0;
char date_str[20];
char time_str[20];
int8_t old_day=32;
int8_t old_hour=24;
uint32_t old_time = 0;

extern uint32_t loop1_count;
extern uint32_t data_count;

char dayDir[40];
char hourDir[10];
char extent[3][4]={"wav","bin","dat"};
int32_t logBuffer[16];

void printStatus(uint32_t num_bytes_written,int16_t vsens)
{
      uint32_t num_samples = num_bytes_written / (4 * NCHAN_ACQ);
      if(Serial)
      { Serial.printf("\t%5d %8d %2d %4d %6d:\t", 
                        loop1_count, num_samples,  acq_missed, mdt, vsens);
        for(int ii=0;ii<9;ii++) Serial.printf("%08x ",logBuffer[ii]);
        Serial.println();
      }
      loop1_count = 0;
      acq_missed = 0;
      mdt=0;
}

uint32_t diskBuffer[NBUF_DISK];

status_t logger(status_t status)
{
  queue.pull(diskBuffer);
  int32_t * buffer=(int32_t*) diskBuffer;

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
    { // create new top folder
      sprintf(dayDir,"/%s_%s",uid_strng,date_str);
      if(!sd.exists(dayDir))
      { sd.mkdir(dayDir);
      }
      // go into top folder
      sd.chdir(dayDir);
      old_day=t.day;
    }
    //
    if(t.hour != old_hour)
    { // go into top folder
      sd.chdir(dayDir);
      // create hourly file folder
      sprintf(hourDir,"%02d",t.hour);
      if(!sd.exists(hourDir))
      { sd.mkdir(hourDir);
      }
      // go into hourly file folder
      sd.chdir(hourDir);        
      old_hour = t.hour;
    }
    // create file name and open file
    char fileName[80];
    sprintf(fileName,"%s_%s_%s.%s",uid_strng,date_str,time_str,extent[PROC_MODE]);
    file=sd.open(fileName, FILE_WRITE);
    if(!file)
    { status=JUST_STOPPED; 
      neo_pixel_show(0, 0, 10);
      return status;
    }
    //
    Serial.print(fileName); Serial.print("; ");
    // initialize file header (wav)
    file.write(&wav_hdr,512);
    num_bytes_written=0;
    status=RECORDING;
    neo_pixel_show(0, 0, 0);
  }
  //
  if((status==RECORDING) || (status==MUST_STOP))
  { // write to disk
    num_bytes_written += write_disk(buffer,4*NBUF_DISK);
    
    // check to close file
    uint32_t tt = rtc_get();
    uint32_t tmp_time=(tt % t_acq );
    if((tmp_time < old_time) || (status == MUST_STOP))
    {
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
      memcpy(logBuffer,buffer, 9*4);
      printStatus(num_bytes_written,vsens);
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
        // check for hibernation
        uint32_t tto = tt / (24*3600);  // seconds to beginning of day
        uint32_t ttx = tt % (24*3600);  // seconds within day
        uint16_t hhx = ttx / 3600;
        if(hhx < h_rec[0])
        { // sleep until h_rec[0]
            uint32_t alarm=tto+h_rec[0]*3600;
            hibernate_until(alarm);
        }
        if((hhx > h_rec[1]) && (hhx < h_rec[2]))
        { // sleep untl h_rec[2]
            uint32_t alarm=tto+h_rec[2]*3600;
            hibernate_until(alarm);
        }
        if((hhx > h_rec[3]))
        { // sleep until h_rec[0]+24
            uint32_t alarm=tto+(24+h_rec[0])*3600;
            hibernate_until(alarm);
        }
        //
        if(t_rep>t_on)                      // if forseen  check for duty cycle
        { uint32_t ttm=tt/60;
          Serial.printf("%d %d %d %d %d\n",t_acq,t_rep,ttm,(ttm % t_rep),t_on);

          uint16_t dt2 = (ttm % t_rep);
          if(dt2>=t_on) 
          {
            adc_exit();
            uint32_t alarm=((ttm/t_rep)+1)*t_rep*60;
            Serial.printf("alarm %d %d %d %d\n",dt2,ttm,tt,alarm);
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
#include "Menu.h"
#define CONFIG_FILE "/config.txt"
static char configText[20*80]={0};  // maximal 30 lines of 80 characters each
static int configIndex[20]={0};     // maximal 30 parameters (actual 11 entries)
//
void storeConfigToFile(void)
{
    FsFile file = sd.open(CONFIG_FILE,(O_RDWR | O_CREAT)); 
    if(file) 
    { file.printf("# configuration file\n");
      file.printf("# should end with '#' or ';' comment may follow\n");
      file.printf("#\n");
      file.printf("!a %d  # file size (sec)\n",t_acq);
      file.printf("!o %d  # on time (min)\n",t_on);
      file.printf("!r %d  # repetition interval (min)\n",t_rep);
      file.printf("!f %d	# sampling frequency (kHz)\n",fsamp/1000);
      file.printf("!g %d	# analog gain (dB)\n",again);
      file.printf("!s %s	# (ISRC) source with sensitivity\n",ISRC);
      file.printf("!c %s	# (ICMS) commissioning organization\n",ICMS);
      file.printf("!n %s	# (IART) name of operator (creator)\n",IART);
      file.printf("!p %s	# (IPRD) project\n",IPRD);
      file.printf("!e %s	# (ISBJ) area\n",ISBJ);
      file.printf("!l %s	# (INAM) location id\n",INAM);
      file.printf("!1 %d	# h_rec[0]\n",h_rec[0]);
      file.printf("!2 %d	# h_rec[1]\n",h_rec[1]);
      file.printf("!3 %d	# h_rec[2]\n",h_rec[2]);
      file.printf("!4 %d	# h_rec[3]\n",h_rec[3]);
      file.printf("!x %s  # start time yyyy-mm-dd_hh:mm:ss\n",startTime);
      file.close(); 
    }
}

inline uint16_t scan16(char *txt) {uint32_t tmp;  sscanf(txt,"%lu",&tmp); return (uint16_t) tmp;}

int16_t loadConfigfromFile(void)
{
  const int nmax=sizeof(configText);
    // load file into memmory
    int imax=0;
    FsFile file = sd.open(CONFIG_FILE); 
    if(file) 
    { while (file.available() && (imax<nmax)) 
      {
        configText[imax++]=file.read();
      }
      file.close(); 
    }
    else
      return 0;
    //
    // find menu entries
    int jj=0;
    for(int ii=0;ii<imax;ii++) {if(configText[ii]=='!') configIndex[jj++]=ii;}
    int jmax=jj;
    // decode menu entries
    for(int ii=0;ii<jmax;ii++)
    { 
      int i1=configIndex[ii]+1;
      int i2=i1+1;
      while(i2<i1+80) {if((configText[i2]=='#')||(configText[i2]==';')) break; i2++;}
      configText[i2]=0;
      char *txt=&configText[i1];
      char *txt2=&txt[2];
      
        switch(txt[0])
        { case 'a': t_acq=scan16(txt2);  break;
          case 'o': t_on =scan16(txt2);  break;
          case 'r': t_rep=scan16(txt2);  break;
          case 'f': fsamp=scan16(txt2);
                    fsamp *=1000; acqModifyFrequency(fsamp); break;
          case 'g': again=scan16(txt2);
                    setAGain((int8_t)again&0xff);  break;
          case 's': sscanf(txt2,"%s",&ISRC[0]);    break; // source (AS1-200)
          case 'c': sscanf(txt2,"%s",&ICMS[0]);    break; // commissioning organisation (WMXZ)
          case 'n': sscanf(txt2,"%s",&IART[0]);    break; // name of operator (creator) (WMXZ)
          case 'p': sscanf(txt2,"%s",&IPRD[0]);    break; // project (Development)
          case 'e': sscanf(txt2,"%s",&ISBJ[0]);    break; // area (atHome)
          case 'l': sscanf(txt2,"%s",&INAM[0]);    break; // location id (B01)
          case '1': h_rec[0]=scan16(txt2);  break; // h_rec[0]
          case '2': h_rec[1]=scan16(txt2);  break; // h_rec[1]
          case '3': h_rec[2]=scan16(txt2);  break; // h_rec[2]
          case '4': h_rec[3]=scan16(txt2);  break; // h_rec[3]
          case 'x': sscanf(txt2,"%s",&startTime[0]); break; // startTime
        }
    }
  return jmax;
}

void configLoad(void)
{ 
  Serial.println(loadConfigfromFile());
}

int configShow(void)
{
  if(loadConfigfromFile()>0)
  {
    Serial.println("config Loaded");
    parameterPrint0();
    Serial.print("ISRC "); Serial.println(ISRC);
    Serial.print("ICMS "); Serial.println(ICMS);
    Serial.print("IART "); Serial.println(IART);
    Serial.print("IPRD "); Serial.println(IPRD);
    Serial.print("ISBJ "); Serial.println(ISBJ);
    Serial.print("INAM "); Serial.println(INAM);

    wavInfoInit();
    return 1;
  }
  return 0;
}
