/* microPAM 
 * Copyright (c) 2026, Walter Zimmer
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
 
#include <Arduino.h>
#include "global.h"
#include "process.h"
#include "utils.h"
#if MCU==T_4_1
  #include "classifier.h"
#endif

/******************************Compress************************************************************/
// temporary storage for processing
#define NDATA NBUF_I2S
#define NCH NCHAN_ACQ

#define NBUF_ACQ NBUF_I2S 	// NOTE: if different need to extract data from I2S buffer
#define NSAMP (NBUF_ACQ/NCH)  // number of samples per buffer

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
    if((unsigned)tmp>amax) amax=tmp;
  }

  // estimate mask (allow only values > 2)
  uint32_t nb=0;
  for(nb=2; nb<=24; nb++) if(amax < (unsigned)(1<<nb)) break;
  nb++;

  uint32_t mask = (1<<nb) -1;

  uint32_t *utmp = (uint32_t *) tempData;
  // mask input data
  for(int ii=nch; ii<ndat; ii++) utmp[ii] &= mask;

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

int32_t *__not_in_flash_func(compressData)(int32_t *buffer, int32_t ndat=NDATA, int nch=NCH)
{
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
#define NBLOCK NBUF_DISK
#if MCU==T_4_1
  #if MAX_QUEUE > 5
    EXTMEM uint32_t queue_buffer[MAX_QUEUE][NBLOCK];
  #else
    DMAMEM uint32_t queue_buffer[MAX_QUEUE][NBLOCK];
  #endif
#else
  #if MAX_QUEUE > 5
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

  int __not_in_flash_func(Queue::push)(uint32_t *data, int ndat)
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

  int __not_in_flash_func(Queue::pull)(uint32_t *data)
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

  int __not_in_flash_func(Queue::available)(void)
  {
    return ((head+MAX_QUEUE-tail)%MAX_QUEUE) >0;
  }

Queue queue;

/******************************Processing*************************************************/
uint32_t acq_missed=0;
uint32_t acq_count=0;
uint32_t proc_time=0;

// ── Synthetic signal injection ────────────────────────────────────────────────
// synth_signal[5][NBUF_ACQ]: 5 consecutive time steps of a test waveform.
// Injected into acq_buffer once every SYNTH_PERIOD calls, for SYNTH_LEN steps.
// Buffer lives in EXTMEM (T4.1 external PSRAM) to avoid consuming SRAM.
// Initially zeroed → injection is a no-op until the signal is filled in.
#define SYNTH_PERIOD 100
#define SYNTH_LEN       4

#ifdef USE_SYNTH_SIGNAL
  #if MCU==T_4_1
    EXTMEM static int32_t synth_signal[SYNTH_LEN][NBUF_ACQ];
  #endif

  static uint32_t proc_call_count = 0;
  static int      synth_step      = -1;   // -1 = idle; 0..SYNTH_LEN-1 = injecting
#endif

void process_init(void)
{
  #ifdef USE_SYNTH_SIGNAL
    proc_call_count = 0;
    synth_step      = -1;
    #if MCU==T_4_1
      synth_signal_fill((float)fsamp);
    #endif
  #endif
}

// Fill synth_signal with a synthetic chirp using synth_chirp_generate().
// Call this once (e.g. from setup) after process_init().
// Parameters are passed directly to synth_chirp_generate for each of the
// SYNTH_LEN time steps, maintaining continuous time across steps.
#ifdef USE_SYNTH_SIGNAL
  #if MCU==T_4_1
  void synth_signal_fill(float fsamp)
  {
    // Fixed signal parameters
    const float aa   = 5000.0f;
    const float bb   = 2.0f;
    const float cc   = 1.5f;
    const float fm   = 15000000.0f;
    const float dd   = 0.0f;
    const float ampl = (float)(1 << 30);

    // Per-channel frame delays: ch0=0, ch1=20, ch2=10, ch3=10
    static const int delay[NCHAN_ACQ] = {0, 20, 10, 10};
    memset(synth_signal, 0, sizeof(synth_signal));
    for (int step = 0; step < SYNTH_LEN; step++)
    {
      // Each step carries a distinct start frequency: f0 = fsamp*(step/8 + 1/64)
      float f0 = fsamp * ((float)step / 8.0f + 1.0f / 64.0f);
      synth_chirp_generate(synth_signal[step], NBUF_ACQ, 0,
                           NCHAN_ACQ, delay,
                           fsamp, aa, bb, cc, f0, fm, dd, ampl);
    }
  }
  #endif
#endif

void __not_in_flash_func(process)(int32_t *acq_buffer)
{ acq_count++;
  uint32_t to=micros();

  // ── inject synthetic signal ─────────────────────────────────────────────
  #ifdef USE_SYNTH_SIGNAL
    proc_call_count++;
    if (synth_step < 0 && (proc_call_count % SYNTH_PERIOD) == 0)
      synth_step = 0;
    #if MCU==T_4_1
      if (synth_step >= 0)
      { for (int ii = 0; ii < NBUF_ACQ; ii++) acq_buffer[ii] += synth_signal[synth_step][ii];
        if (++synth_step >= SYNTH_LEN) synth_step = -1;
      }
    #endif
  #endif

  #if PROC_MODE==0
    if(!queue.push((uint32_t*)acq_buffer,NBUF_ACQ)) acq_missed++;
  #elif PROC_MODE==1
    // shift to right to remove trailing zeros and minimize noise
    for(int ii=0;ii<NDATA;ii++) acq_buffer[ii]=acq_buffer[ii]>>SHIFT;
    if(!queue.push((uint32_t*)compressData(acq_buffer,NDATA,NCH),acq_buffer[NBUF_ACQ-1])) acq_missed++;
  #elif PROC_MODE==2 || PROC_MODE==3 || PROC_MODE==4
    #if MCU==T_4_1
    dsp_trigger(acq_buffer);
    #endif
  #endif
  //
  uint32_t dt=micros()-to;
  if (dt>proc_time) proc_time=dt;
}

#if (MCU==T_4_1)
  #include "DSP/cmsis.h"

  DMAMEM int32_t procBuffer[NBUF_I2S];

  #define NFFT (2*NSAMP)        // will result in NSAMP complex spectral values

  arm_rfft_fast_instance_f32 S;

  float O[NBUF_ACQ];  // NBUF_ACQ = 2048 -> NSAMP=512 ->NFFT=1024
  float W[NFFT];
  float X[NFFT];
  float Y[NFFT];
  float Z[NFFT*NCH];  // here we have for each channel NSAMP complex values
  
  float scale1 = 1.0f/(float)(1<<31);
  float scale2 = (float)(1<<(31-SHIFT));   // spectrum: preserves bit-shift headroom
  float scale3 = (float)(1<<(32-SHIFT));   // intensity: slightly less headroom than spectrum

  inline void RFFT(float *Y, float *X) { arm_rfft_fast_f32( &S, X, Y,0); }

  void spectrum_init(void)
  {   arm_rfft_fast_init_f32( &S, NFFT);

      for(int jj=0; jj<NBUF_ACQ; jj++) O[jj]=0.0f;
      for(int jj=0; jj<NFFT; jj++) W[jj]=(1.0f-cosf(2.0f*3.14159265359f*(float)jj/(float)NFFT))/2.0f;
  }

  void spectrum_apply(int32_t *buffer)
  {
    const float inv_sqrt_nfft = 1.0f / sqrtf((float)NFFT);
    for(int ii=0; ii<NCH; ii++)
    { // 50% overlap
      for(int jj=0; jj<NSAMP; jj++) X[jj]=O[ii+NCH*jj];
      for(int jj=0; jj<NSAMP; jj++) X[NSAMP+jj]=O[ii+NCH*jj]=((float)buffer[ii+NCH*jj])*scale1;
      //for(int jj=0; jj<NFFT; jj++) X[jj] *= W[jj];
      RFFT(Y,X);
      // Pack Z as interleaved (re,im) pairs: Z[2*(ii+NCH*k)], Z[2*(ii+NCH*k)+1]
      // so that spectrum_power and intensity_apply can use a single flat index.
      // DC bin (real only; imaginary = 0 by definition)
      Z[2*(ii+NCH*0)]   = Y[0] * inv_sqrt_nfft;
      Z[2*(ii+NCH*0)+1] = 0.0f;
      // Nyquist bin Y[1]: not needed, set to zero
      // (bin NSAMP = fs/2; discarded because it carries no phase information)
      // Bins 1..NSAMP-1: complex pairs
      for(int kk=1; kk<NSAMP; kk++)
      { Z[2*(ii+NCH*kk)]   = Y[2*kk]   * inv_sqrt_nfft;
        Z[2*(ii+NCH*kk)+1] = Y[2*kk+1] * inv_sqrt_nfft;
      }
    }
  }

  // directional intensity sums negative imaginary part of hydrophone pair crosscorrelation
  float I[3*NSAMP];   // here we have 3 values (x,y,z) intensity values

  // tetraheder (*4)
  //[[ 1  0  1 -1  0  1]
  // [ 1  1  0  0 -1 -1]
  // [ 0  1  1  1  1  0]]
  //
  float M[3][6]=
                {{1.0f, 0.0f, 1.0f,-1.0f, 0.0f, 1.0f},
                 {1.0f, 1.0f, 0.0f, 0.0f,-1.0f,-1.0f},
                 {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f}};


  void intensity_init()
  {
  }

  float Dmax=0.0f;
  float Dmean=0.0f;
  float Dsnr=0.0f;
  float Dpeak=0.0f;
  float D[NSAMP];

  void intensity_apply(void)
  { int kk,i0,i1;
    for(int ii=0; ii<3; ii++)
    { float *Mi=M[ii];
      for(int jj=0;jj<NSAMP;jj++)
      { kk=ii+3*jj;
        I[kk]=0.0f;
        //0-1
        i0=2*(0+4*jj);
        i1=2*(1+4*jj);
        I[kk] += -Mi[0]*(Z[i0+1]*Z[i1]-Z[i0]*Z[i1+1]);
        //0-2
        i0=2*(0+4*jj);
        i1=2*(2+4*jj);
        I[kk] += -Mi[1]*(Z[i0+1]*Z[i1]-Z[i0]*Z[i1+1]);
        //0-3
        i0=2*(0+4*jj);
        i1=2*(3+4*jj);
        I[kk] += -Mi[2]*(Z[i0+1]*Z[i1]-Z[i0]*Z[i1+1]);
        //1-2
        i0=2*(1+4*jj);
        i1=2*(2+4*jj);
        I[kk] += -Mi[3]*(Z[i0+1]*Z[i1]-Z[i0]*Z[i1+1]);
        //1-3
        i0=2*(1+4*jj);
        i1=2*(3+4*jj);
        I[kk] += -Mi[4]*(Z[i0+1]*Z[i1]-Z[i0]*Z[i1+1]);
        //2-3
        i0=2*(2+4*jj);
        i1=2*(3+4*jj);
        I[kk] += -Mi[5]*(Z[i0+1]*Z[i1]-Z[i0]*Z[i1+1]);
      }
    }
    // compute instantaneous intensity magnitude for each frequency bin
    for(int jj=0;jj<NSAMP;jj++) D[jj]= sqrtf(I[3*jj]*I[3*jj] +I[1+3*jj]*I[1+3*jj] +I[2+3*jj]*I[2+3*jj]);
  }

  void detection_init(void)
  { Dmean = 0.0f;
    Dpeak = 0.0f;
  }

  void detection_apply(void)
  {
    // peak of this block
    Dmax = 0.0f;
    for(int jj=0;jj<NSAMP;jj++) if(D[jj]>Dmax) Dmax=D[jj];

    // mean of this block
    float Dblock = 0.0f;
    for(int jj=0;jj<NSAMP;jj++) Dblock += D[jj];
    Dblock /= (float)NSAMP;

    // exponential average of block mean → background estimate
    // slow adaptation during detections so the background is not contaminated
    float alpha = (Dsnr > DETECT_THR) ? DETECT_ALPHA * 0.1f : DETECT_ALPHA;
    if(Dmean == 0.0f) Dmean = Dblock;   // seed on first call
    else              Dmean += alpha * (Dblock - Dmean);

    // signal: block mean relative to background
    Dpeak = Dmax;
    Dsnr  = (Dmean > 0.0f) ? Dblock / Dmean : 0.0f;

  }

  int32_t *spectrum_power(int32_t *buffer)
  { for(int ii=0;ii<NBUF_ACQ; ii++) procBuffer[ii]=buffer[ii];
    spectrum_apply(procBuffer);
    for(int ii=0;ii<NDATA;ii++)
    { float re=Z[2*ii], im=Z[2*ii+1];
      buffer[ii]=(int32_t)(sqrtf(re*re+im*im)*scale2);
    }
    return buffer;
  }

  float Imax=0.0f;
  int32_t *dsp_apply(int32_t *buffer)
  { for(int ii=0;ii<NBUF_ACQ; ii++) procBuffer[ii]=buffer[ii];
    spectrum_apply(procBuffer);
    intensity_apply();
    detection_apply();
    classifier_trigger(D, NSAMP);

    for(int ii=0;ii<3*NSAMP; ii++) I[ii]=I[ii]*scale3;
    for(int ii=0;ii<3*NSAMP; ii++) if(I[ii]>Imax) Imax=I[ii];
    for(int ii=0;ii<3*NSAMP; ii++) buffer[ii]= (int32_t) I[ii];
    return buffer;
  }

  // ── Async DSP IRQ (modes 2/3/4) ─────────────────────────────────────────────
  #define DSP_IRQ          IRQ_QTIMER3
  #define DSP_IRQ_PRIORITY (7 * 16)   // below DMA (6*16), above classifier (8*16)

  volatile bool dsp_busy = false;
  DMAMEM static int32_t dsp_buffer[NBUF_I2S]; // snapshot of acq_buffer for DSP ISR

  void dsp_isr(void)
  {
    NVIC_CLEAR_PENDING(DSP_IRQ);
    #if PROC_MODE==2
    { int32_t *dest = spectrum_power(dsp_buffer);
      if(!queue.push((uint32_t*)compressData(dest,NDATA,NCH), dest[NBUF_ACQ-1])) acq_missed++;
    }
    #elif PROC_MODE==3
    { int32_t *dest = dsp_apply(dsp_buffer);
      if(!queue.push((uint32_t*)compressData(dest,NBUF_PROC,NCHAN_PROC), dest[NBUF_ACQ-1])) acq_missed++;
    }
    #elif PROC_MODE==4
      dsp_apply(dsp_buffer);   // classifier ISR owns queue push
    #endif
    dsp_busy = false;
  }

  void dsp_trigger(int32_t *acq_buffer)
  { if(dsp_busy) { acq_missed++; return; }
    dsp_busy = true;
    for(int ii=0;ii<NBUF_ACQ;ii++) dsp_buffer[ii]=acq_buffer[ii];
    NVIC_SET_PENDING(DSP_IRQ);
  }

  void dsp_init(void)
  { spectrum_init();
    intensity_init();
    detection_init();
    classifier_init();
    attachInterruptVector(DSP_IRQ, dsp_isr);
    NVIC_SET_PRIORITY(DSP_IRQ, DSP_IRQ_PRIORITY);
    NVIC_ENABLE_IRQ(DSP_IRQ);
  }
#endif
