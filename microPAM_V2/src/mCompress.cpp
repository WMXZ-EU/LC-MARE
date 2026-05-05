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
 
#include <stdint.h>
#include <string.h>

//#include "api/ArduinoAPI.h"
#include "Arduino.h"

#include "mConfig.h"
#include "mQueue.h"
#include "mACQ.h"
#include "mCompress.h"
#include "mRTC.h"


#define NH 6
#define NBLOCK NBUF_ACQ

static uint32_t tempData[NBUF_ACQ];
static uint32_t outData[NBUF_ACQ];
static uint32_t dout[NBLOCK];

int32_t *tempDatai=(int32_t*) tempData;
int32_t tempData0[NCH];

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
  // copy reference (first sample of all channels)
  for (int  ii = 0; ii < NCH; ii++) tempData0[ii] = tempDatai[ii] = din[ii];
  
  //differentiate (equiv 6 dB/Octave HP filter)
  for (int  ii = NCH; ii < NBUF_ACQ; ii++) tempDatai[ii] = (din[ii] - din[ii - NCH]);

  // find maximum in filtered data
  int32_t mx = 0;
  for (int ii = NCH; ii < NBUF_ACQ; ii++)
  {
    int32_t dd =  tempDatai[ii];
    if(dd<0)  dd = -dd;
    if(dd>mx) mx = dd;
  }

  // estimate mask (allow only values > 2)
  int nb;
  for(nb=2; nb<MB; nb++) if(mx < (1<<(nb-1))) break;
  // compression factor (32/nb)
  proc_stat[nb-1]++;
  if((uint32_t)nb>max_stat) max_stat=nb;

  // mask data (all but first sample) (mask needed for negative numbers)
  uint32_t msk = (1 << nb) - 1;
  for (int ii = NCH; ii < NBUF_ACQ; ii++) { tempData[ii] &= (uint32_t)msk; }

  // pack all data
  int ncmp = (NBUF_ACQ*nb) / MBIT;
  int ndat = NH+NCH + ncmp;
  int ndat0 = ndat; 

  // ensure that ndat is even (to allow fast access to header)
  ndat= ((ndat>>1) + 1)<<1;

    // clean data store
  for (int ii = 0; ii < NBUF_ACQ; ii++) outData[ii]=0;

  // prepare header
  uint32_t *iptr=(uint32_t *) outData;
  *iptr++ = 0xA5A5A5A5;
  *iptr++ = nb | shift<<16;
  *iptr++ = to;
  *iptr++ = t1;
  *iptr++ = NCH;
  *iptr++ = NCH+ncmp; // number of data after header
  //
  int kk=NH;
  outData[kk++] = tempData[0]; tempData[0] = 0;

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

  if ((nout + ndat) <= NBLOCK)
  { // all data fit in current block
      for (int ii = 0; ii < ndat; ii++) dout[nout++] = outData[ii];
  }
  else if ((nout + NH) > NBLOCK) //avoid partial header (special case)
  {
      while(nout<NBLOCK) dout[nout++] = 0; // fill rest of block with zero
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
      nr = NBLOCK-nout;  //remaining data
      uint32_t *iptr = (uint32_t *) outData;
      // correct header
      iptr[5] = (iptr[5]<<16) | (nr-NH);  //orig remaining data | actual remaining data after header 

      while (nout < NBLOCK) dout[nout++] = outData[ii++];
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
