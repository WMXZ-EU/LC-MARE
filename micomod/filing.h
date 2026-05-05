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
 
#ifndef _FILING_H
#define _FILING_H

#include <TimeLib.h>

int16_t diskBuffer[NBUF*NSAMP];
int16_t haveSD=0;

File file=NULL; 

uint32_t SerNum;
char header[512];
char dirName[128];
char fileName[128];

uint32_t getTeensySerial(void);
int16_t checkEndOfFile(int16_t status);
int16_t newDay(void);
int16_t newHour(void);
//int16_t checkDiskSpace(void);
int16_t newDirectory(char *dirName);
int16_t newFileName(char *fileName);
int16_t makeHeader(char *header);
int16_t storeData(int status);

uint32_t disk_count=0;

uint32_t tx_ = 0;
int16_t checkEndOfFile(int16_t status)
{ uint32_t tx=rtc_get();
  tx = tx % t_acq;
  if((status>1) && (tx_>0) && (tx < tx_)) status=3;
  tx_=tx;
  return status;  
}

int32_t day_   = 0;
int16_t newDay(void)
{   uint32_t tx=rtc_get();
    int32_t d_ = (int32_t)tx/SECS_PER_DAY; // use days since 1970 as measure
    if(day_== d_) return 0;
    day_ = d_;
    return 1;
}

int32_t hour_ = 0;
int16_t newHour(void)
{   uint32_t tx=rtc_get();
    int32_t h_ = (int32_t) tx/SECS_PER_HOUR;  // use hours since 1970 as measures
    if(hour_== h_) return 0;
    hour_ = h_;
    return 1;
}

#if defined ARDUINO_TEENSY40 || defined ARDUINO_TEENSY41
  uint32_t getTeensySerial(void) 
  {
    uint32_t num;
    num = HW_OCOTP_MAC0 & 0xFFFFFF;
    return num;
  }
#else
  uint32_t getTeensySerial(void)  { return 0;}
#endif

int16_t newDirectory(char *dirName)
{ if(newHour())
    {   
        tmElements_t tm;
        breakTime(rtc_get(), tm);
        sprintf(dirName, "/%s%06x_%04d%02d%02d/%02d/", 
                      DirPrefix,(unsigned int)SerNum,
                              tmYearToCalendar(tm.Year),tm.Month, tm.Day, tm.Hour);
        //
        Serial.print("\n"); Serial.print(dirName);
        return 1;
    }
    return 0;
}

int16_t newFileName(char *fileName)
{
    tmElements_t tm;
    breakTime(rtc_get(), tm);
    
    sprintf(fileName, "/%s%06x_%04d%02d%02d/%02d/%s_%02d%02d%02d.bin", 
            DirPrefix,(unsigned int)SerNum,
                      tmYearToCalendar(tm.Year),tm.Month, tm.Day, tm.Hour, 
                      FilePrefix, tm.Hour, tm.Minute, tm.Second);
    //
    Serial.print("\n"); Serial.print(": ");Serial.print(fileName);
    return 1;
}

int16_t makeHeader(char *header)
{
    /**
     * @brief Make file header
     * @param header is pointer to header
     * 
     */
    #define MAGIC "WMXZ"
    tmElements_t tm;
    breakTime(rtc_get(), tm);

    int nd=sprintf(header,"%s%04d%02d%02d_%02d%02d%02d",
            MAGIC,tmYearToCalendar(tm.Year),tm.Month,tm.Day,tm.Hour,tm.Minute,tm.Second);
    char *ptr = header+(nd+1);
    int32_t *iptr = (int32_t *) ptr;
    //float *fptr = (float *) ptr;
    
    // to be filled in
    iptr[0] = 10;                   // SW version
    iptr[1] = (int32_t)SerNum;      // serial number
    iptr[2] = fsamp;
    iptr[3] = nch;
    iptr[4] = t_acq;
    iptr[5] = t_on;
    iptr[6] = t_off;
    iptr[7] = DO_COMPRESS;
    iptr[8] = shift;

    uint32_t *uptr = (uint32_t*) header;
    uptr[127] = 0x55555555;
    //
    return 1;
}
int16_t ErrorMsg(const char *txt)
{ Serial.println();
  Serial.println(txt);
  return -1;
}

void storeBegin(void)
{
  for(int ii=0;ii<5;ii++)
  {
    if(!(SD.begin(BUILTIN_SDCARD))) 
    { // stop here if no SD card, but print a message
      Serial.println("Unable to access the SD card");
      delay(100);
    }
    else
    { Serial.println("SD card found");
      haveSD=1;
      break;
    }
  }
}

int16_t storeData(int status)
{
  int32_t nbuf=sizeof(diskBuffer);

  if(!haveSD) return status;

  switch(status)
  {
    case 0:
          // open file
          if(newDirectory(dirName)) 
          {   if( !SD.exists(dirName) && 
                  !SD.mkdir(dirName)) return ErrorMsg("Error Dir");
          }
          //
          if(!newFileName(fileName))return ErrorMsg("Failing new file");
          
          file = SD.open(fileName, FILE_WRITE_BEGIN); 
          if(!file) return ErrorMsg("Failing open file");
          //
          status=1;
          break;
    case 1:
          // write header
          makeHeader(header);
          if(file.write((const void*)header,512) < 512) return ErrorMsg("Error Header");
          //
          status=2;
          break;
    case 2:
          // write data
          disk_count++;
          if(file.write((const void *)diskBuffer, nbuf) < (size_t) nbuf) return ErrorMsg("Error Data");
          break;
    case 3:
          // close file
          file.close();
          status=0;
          break;
    case 4:
          // close file and stop
          file.close();
          status=-1;
          break;
    case 5:
          // close file and hibernate
          file.close();
          status=-1; // should hibernate here
          break;
    default:
          status=-1;
  }
  return status;
}
#endif
