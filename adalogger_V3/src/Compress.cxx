/* microPAM 
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
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
#include "global.h"
#include "Compress.h"

// temporary storage for processing
#define NDATA NBUF_ACQ
int32_t tempData[NDATA];
uint32_t *utemp = (uint32_t *) tempData;


int32_t __not_in_flash_func(encodeBlock)(uint32_t *uout, uint32_t *uinp,  int32_t ndata, int32_t nb, int32_t MB)
{   int nx = MB;
    int kk = 0;
    for (int ii = 0; ii < ndata; ii++)
    {   nx -= nb;
        if(nx > 0)
        {   uout[kk] |= uinp[ii] << nx;
        }
        else if(nx==0) 
        {   uout[kk++] |= uinp[ii];
            nx=MB;
        } 
        else    // nx is < 0
        {   uout[kk++] |= uinp[ii] >> (-nx);
            nx += MB;
            uout[kk] = uinp[ii] << nx;
        }
    }
    return (nx==MB)? kk : kk+1;
}

int32_t __not_in_flash_func(encodeData)(uint32_t *out, int32_t *inp, int ndat, int nch)
{
  // copy data (differences) to temporary storage and clean input/output buffer
  for(int ii=0; ii<nch;ii++) tempData[ii]=inp[ii];
  // differentiate along channels
  for(int ii=nch; ii<ndat; ii++)
  { tempData[ii] = inp[ii]-inp[ii-nch];
  }
  // clear input to to used as output
  for(int ii=0;ii<ndat;ii++) inp[ii]=0;

  // find absolute maximum
  uint32_t amax=0;
  for(int ii=nch; ii<ndat; ii++) 
  { int32_t tmp;
    tmp=tempData[ii];
    if(tmp<0) tmp=-tmp;
    if(tmp>amax) amax=tmp;
  }

  // estimate mask (allow only values > 2)
  uint32_t nb=0;
  for(nb=2; nb<=24; nb++) if(amax < (1<<nb)) break;
  nb++;

  uint32_t mask = (1<<nb) -1;

  uint32_t *utmp = (uint32_t *) tempData;
  // mask input data
  for(int ii=nch; ii<NDATA; ii++) utmp[ii] &= mask;

  out[0]=0xA5A5A5A5;
  out[1]=millis();
  out[2]=nb;
  out[3]=0;
  for(int ii=0; ii<nch;ii++) {out[4+ii]=tempData[ii]; tempData[ii]=0;}
  int32_t nd = encodeBlock(&out[4+nch],utmp,ndat, nb,MBIT);

  out[3]=nd;
  //
  out[NDATA-1]=4+nch+nd;
  return 4+nch+nd;
}

int32_t *__not_in_flash_func(compressData)(int32_t *buffer)
{
  int32_t ndat=NDATA;
  int nch=NCH;
  //
  // shift to right to remove trailing zeros and minimize noise
  for(int ii=0;ii<NDATA;ii++) buffer[ii]=buffer[ii]>>SHIFT;
  //
  //reuse input buffer also as output buffer;
  uint32_t *outData  = (uint32_t *) buffer;
  //
  int kk = 0;
  kk = encodeData(outData,buffer,ndat, nch); 
  // kk point to next free buffer location
  //
  if (kk < NDATA) // should be always the case
  {
    // ceil to 512 block limit and indicate new length of buffer
    uint32_t nbuf=((kk+127)/128)*128;
    for (;kk<nbuf;kk++) outData[kk]=0;
    outData[NDATA-1]=nbuf;
  }
  return buffer;
}

// below earlier version (for reference only; has different header)
#if 0
/*
// temporary storage for processing
int32_t tempData[NDATA];
uint32_t *utemp = (uint32_t *) tempData;

int32_t storeData(int32_t *buffer)
{ 
  int32_t ndat=0;
  //
  // shift to right to remove trailing zeros and minimize noise
  for(int ii=0;ii<NBUF_I2S;ii++) buffer[ii]=buffer[ii]>>SHIFT;
  //
  //reuse input buffer also as output buffer;
  uint32_t *outData  = (uint32_t *) buffer;
  //
  int nch=1;
  int kk = 0;
  for(int mm=0; mm<NBUF_I2S; mm+=NDATA)
  { 

    // copy data (differences) to temporatory storage and clean input/output buffer
    tempData[0]=buffer[mm];
    for(int ii=0; ii<NDATA; ii++)
    { tempData[ii] = buffer[mm+ii]-buffer[mm+ii-nch];
      outData[mm+ii-nch]=0;   // clears also input buffer
    }
    outData[mm+NDATA-nch]=0;

    // find absolute maximum
    uint32_t amax=0;
    for(int ii=nch; ii<NDATA; ii++) 
    { int32_t tmp;
      tmp=tempData[ii];
      if(tmp<0) tmp=-tmp;
      if(tmp>amax) amax=tmp;
    }

    // estimate mask (allow only values > 2)
    uint32_t nb=0;
    for(nb=2; nb<=24; nb++) if(amax < (1<<nb)) break;
    nb++;

    uint32_t ncmp = (NDATA*nb) / MBIT;
    uint32_t mask = (1<<nb) -1;

    // mask input data
    for(int ii=nch; ii<NDATA; ii++) utemp[ii] &= mask;

    // pack data
    outData[kk++]=0xA5A5A5A5;
    outData[kk++]=nb;
    outData[kk++]=ncmp;
    for(int ii=0; ii<nch;ii++) outData[kk++]=tempData[ii];
    //
    int nx = MBIT;
    for (int ii = nch; ii < NDATA; ii++)
    {   nx -= nb;
        if(nx > 0)
        {   outData[kk] |= (utemp[ii] << nx);
        }
        else if(nx==0) 
        {   outData[kk++] |= utemp[ii];
            nx=MBIT;
        } 
        else    // nx is < 0
        {   outData[kk++] |= (utemp[ii] >> (-nx));
            nx += MBIT;
            outData[kk] = (utemp[ii] << nx);
        }
    }
    if (!(nx==MBIT)) kk++; // next output word
    // advance to next block
  }
  //
  // ceil to 512 block limit
  uint32_t nbuf=((kk+127)/128)*512;
  for (;kk<nbuf/4;kk++) buffer[kk]=0;
  //
  return write_disk(buffer,nbuf);
}
*/
#endif