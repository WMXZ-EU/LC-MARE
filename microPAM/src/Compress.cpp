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
 
#include <stdint.h>
#include <string.h>

//#include "api/ArduinoAPI.h"
#include "Arduino.h"

#include "Config.h"
#include "Queue.h"
#include "ACQ.h"
#include "Compress.h"
#include "RTC.h"

#define NH 6
#define NBUF_OUT NBUF_ACQ

static uint32_t tempData[NBUF_ACQ];
static uint32_t outData[NBUF_ACQ];
static uint32_t dout[NBUF_OUT];

int32_t *tempDatai=(int32_t*) tempData;

uint32_t proc_stat[MB];
uint32_t max_stat;
int __not_in_flash_func(compress)(void *inp)
{   
  int ret=1;
  uint32_t to;
  to = rtc_get();
  uint32_t t1;
  t1 = micros();

  int32_t *din = (int32_t *) inp;
  //
  // copy data 
  for (int  ii = 0; ii < NBUF_ACQ; ii++) tempDatai[ii] = din[ii];
  
  //differentiate (equiv 6 dB/Octave HP filter) all but the first NCHAN_ACQ data
  for (int  ii = NCHAN_ACQ; ii < NBUF_ACQ; ii++) tempDatai[ii] -=  din[ii - NCHAN_ACQ];

  // find maximum in filtered data 
  int32_t mx = 0;
  for (int ii = NCHAN_ACQ; ii < NBUF_ACQ; ii++)
  {
    int32_t dd =  tempDatai[ii];
    if(dd<0)  dd = -dd; // take absolut value
    if(dd>mx) mx =  dd; // take maximum
  }

  // estimate mask (allow only values > 2)
  int nb;
  for(nb=2; nb<MB; nb++) if(mx < (1<<(nb-1))) break;
  // compression factor (32/nb)

  // keep statistics
  proc_stat[nb-1]++;
  if((uint32_t)nb>max_stat) max_stat=nb;

  // mask data (all but first sample) (mask needed for negative numbers)
  uint32_t msk = (1 << nb) - 1;
  for (int ii = NCHAN_ACQ; ii < NBUF_ACQ; ii++) { tempData[ii] &= (uint32_t)msk; }

  // pack all data
  int ncmp = (NBUF_ACQ*nb) / MBIT;
  int ndat = NH + NCHAN_ACQ + ncmp;
  int ndat0 = ndat; 

  // ensure that ndat is even (to allow fast access to header)
  ndat= ((ndat>>1) + 1)<<1;

    // clean data store
  for (int ii = 0; ii < NSAMP; ii++) outData[ii]=0;

  // prepare header
  uint32_t *iptr=(uint32_t *) outData;
  *iptr++ = 0xA5A5A5A5;
  *iptr++ = nb | shift<<16;
  *iptr++ = to;
  *iptr++ = t1;
  *iptr++ = NCHAN_ACQ;
  *iptr++ = NCHAN_ACQ+ncmp; // number of data after header
  //
  int kk=NH;
  outData[kk++] = tempData[0]; tempData[0] = 0;
  #if NCHAN_ACQ>1
    outData[kk++] = tempData[1]; tempData[1] = 0;
  #endif
  #if NCHAN_ACQ>2
    outData[kk++] = tempData[2]; tempData[2] = 0;
  #endif
  #if NCHAN_ACQ>3
    outData[kk++] = tempData[3]; tempData[3] = 0;
  #endif
  #if NCHAN_ACQ>4
    outData[kk++] = tempData[4]; tempData[4] = 0;
  #endif
  #if NCHAN_ACQ>5
    outData[kk++] = tempData[5]; tempData[5] = 0;
  #endif
  #if NCHAN_ACQ>6
    outData[kk++] = tempData[6]; tempData[6] = 0;
  #endif
  #if NCHAN_ACQ>7
    outData[kk++] = tempData[7]; tempData[7] = 0;
  #endif
  #if NCHAN_ACQ>8
    #error "NCH>8"
  #endif

  // pack data
  // 
  int nx = MBIT;
  for (int ii = 0; ii < NBUF_ACQ; ii ++)
  {   nx -= nb;
      if(nx > 0)
      {   outData[kk] |= (tempData[ii] << nx);
      }
      else if(nx==0) 
      {   outData[kk++] |= tempData[ii];
          nx=MBIT;
      } 
      else    // nx is < 0
      {   outData[kk++] |= (tempData[ii] >> (-nx));
          nx += MBIT;
          outData[kk] = (tempData[ii] << nx);
      }
  }

  // store actual data
  static int nout=0;

  if ((nout + ndat) <= NBUF_OUT)
  { // all data fit in current block
      for (int ii = 0; ii < ndat; ii++) dout[nout++] = outData[ii];
  }
  else if ((nout + NH) > NBUF_OUT) //avoid partial header (special case)
  {
      while(nout<NBUF_OUT) dout[nout++] = 0; // fill rest of block with zero
      // store data
      if(!pushData(dout)) ret = 0;
      //
      // store data in next block
      nout=0;
      for (int ii=0; ii < ndat; ii++) dout[nout++] = outData[ii];
  }
  else
  { // data crosses two blocks
      int ii=0;
      int nr;
      nr = NBUF_OUT-nout;  //remaining data
      uint32_t *iptr = (uint32_t *) outData;
      // correct header
      iptr[5] = (iptr[5]<<16) | (nr-NH);  //orig remaining data | actual remaining data after header 

      while (nout < NBUF_OUT) dout[nout++] = outData[ii++];
      // store data
      if(!pushData(dout)) ret = 0;
      //
      // store rest in next block
      nr=ndat0-ii; // for header
      // add blockHeader continuation
      iptr[5]=(iptr[5] & 0xffff0000) | nr; //orig remaining data | actual remaining data after header
      // copy first header
      for(nout=0;nout<NH;nout++) dout[nout] = outData[nout];
      // followed by rest of data
      while (ii < ndat) dout[nout++] = outData[ii++];
  }
  return ret;
}
