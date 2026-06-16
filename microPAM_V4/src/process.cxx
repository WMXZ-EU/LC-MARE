#include <Arduino.h>
#include "global.h"
#include "process.h"

/******************************Compress************************************************************/
// temporary storage for processing
#define NDATA NBUF
#define MBIT 32
#define NCH NCHAN_ACQ

int32_t tempData[NDATA];
uint32_t *utemp = (uint32_t *) tempData;

int32_t encodeBlock(uint32_t *uout, uint32_t *uinp,  int32_t ndata, int32_t nb, int32_t MB)
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

int32_t encodeData(uint32_t *out, int32_t *inp, int ndat, int nch)
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
    if((unsigned)tmp>amax) amax=tmp;
  }

  // estimate mask (allow only values > 2)
  uint32_t nb=0;
  for(nb=2; nb<=24; nb++) if(amax < (unsigned)(1<<nb)) break;
  nb++;

  uint32_t mask = (1<<nb) -1;

  uint32_t *utmp = (uint32_t *) tempData;
  // mask input data
  for(int ii=nch; ii<NDATA; ii++) utmp[ii] &= mask;

  int kk=0;
  out[kk++]=0xA5A5A5A5;
  out[kk++]=millis();     // is missing in V2
  out[kk++]=nb;
  out[kk++]=0;
  for(int ii=0; ii<nch;ii++) {out[kk+ii]=tempData[ii]; tempData[ii]=0;}
  int32_t nd = encodeBlock(&out[kk+nch],utmp,ndat, nb,MBIT);

  out[kk-1]=nd;
  //
  kk +=(nch+nd);
  out[NDATA-1]=kk;
  return kk;
}

int32_t *compressData(int32_t *buffer)
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
  uint32_t kk = 0;
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

/***************************Queue*****************************************************************/
#if MCU==T_4_1
  #if MAX_QUEUE >10
    EXTMEM uint32_t queue_buffer[MAX_QUEUE][NBLOCK];
  #else
    uint32_t queue_buffer[MAX_QUEUE][NBLOCK];
  #endif
#else
  #if MAX_QUEUE >5
    uint32_t queue_buffer[MAX_QUEUE][NBLOCK]  PSRAM ;
  #else
    uint32_t queue_buffer[MAX_QUEUE][NBLOCK];
  #endif
#endif

 #define INC(x) ((x+1)%MAX_QUEUE)

  void Queue::reset(void)
  { // clear queue buffer
    for(int ii=0; ii<MAX_QUEUE;ii++)
      for(int jj=0;jj<NBLOCK;jj++) queue_buffer[ii][jj]=0;
    head=0;
    tail=0;
    busy=0;
    cnt=0;
  }

  int Queue::push(uint32_t *data, int ndat)
  { while(busy);
    busy=1;
    if((cnt+ndat)<=NBLOCK)
    { for(int ii=0;ii<ndat;ii++,cnt++)  queue_buffer[head][cnt]=data[ii];
      busy=0;
      return 1;
    }
    else
    { int nbuf=cnt;
      if(nbuf<NBLOCK-1)
      {
        for(;cnt<NBLOCK;cnt++)  queue_buffer[head][cnt]=0;
        queue_buffer[head][NBLOCK-1]=nbuf;
      }
      cnt=0;
      head=INC(head);
      if(head==tail) 
      { busy=0; 
        return 0;
      }
      for(int ii=0;ii<ndat;ii++,cnt++)  queue_buffer[head][cnt]=data[ii];
      busy=0;
      return 1;
    }
  }

  int Queue::pull(uint32_t *data)
  { while(busy);
    busy=1;
    if(tail==head) 
    { busy=0;
      return 0;
    }
    for(int ii=0; ii<NBLOCK;ii++) data[ii]=queue_buffer[tail][ii];
    tail=INC(tail);
    busy=0;
    return 1;
  }

  int Queue::available(void)
  {
    return ((head+MAX_QUEUE-tail)%MAX_QUEUE) >0;
  }

Queue queue;

/******************************Processing*************************************************/
uint32_t acq_missed=0;
uint32_t acq_count=0;
uint32_t proc_time=0;

void process(int32_t * buffer)
{ acq_count++;
  
  uint32_t to=micros();
  //
  #if PROC==0
    if(!queue.push((uint32_t*)buffer,NBUF_I2S)) acq_missed++;
  #else
    if(!queue.push((uint32_t*)compressData(buffer),buffer[NBUF_I2S-1])) acq_missed++;
  #endif
  //
  uint32_t dt=micros()-to;
  if (dt>proc_time) proc_time=dt;
}
