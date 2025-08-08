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
#ifndef FILING_H
#define FILING_H

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
      unsigned short  nBitsPerSamples;//36
      char    lId[4];                 //40
      unsigned int  lLen;             //44
      char    iId[4];                 //48
      char    info[512-14*4];         // fill header to 512 bytes (504=48+546)
      char    dId[4];                 //508
      unsigned int    dLen;           //512
  } HdrStruct;

  uint16_t SD_init(void);
  status_t logger(int32_t * buffer,status_t status);

  void configLoad(void);
  void configShow(void);

#endif