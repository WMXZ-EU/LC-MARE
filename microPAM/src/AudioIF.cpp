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
#if defined(__IMXRT1062__) 
  #include "Arduino.h"
  #if defined(AUDIO_INTERFACE)
    #include "Config.h"
    #include "AudioIF.h"

    /*********************** Audio queue *************************************/
    #define MAUDIO 10
    static int32_t audio_buffer[MAUDIO][NBUF_I2S];
    static int head=0;
    static int tail=0;
    
    int16_t __not_in_flash_func(putAudio)(int32_t *data)
    {
      if ( (tail+1)%MAUDIO == head ) return 0;    // queue is full
      for(int ii=0;ii<NBUF_I2S;ii++) audio_buffer[tail][ii]=data[ii];
      tail = (tail+1)%MAUDIO;
      return 1;   // signal success.
    }

    int16_t __not_in_flash_func(getAudio)(int32_t *data)
    {
      if ( head==tail ) return 0;                 // queue is empty
      for(int ii=0;ii<NBUF_I2S;ii++) data[ii]=audio_buffer[head][ii];
      head = (head+1)%MAUDIO;
      return 1;   // signal success.
    }

    /********************** Audio Interface **********************************/
    void AudioIF::extract(int16_t *dst1, int16_t *dst2, const int32_t *src)
    {
      for(int ii=0,jj=0; ii<NBUF_ACQ; ii++) 
      { dst1[ii]=(int16_t)src[jj++];   
        dst2[ii]=(int16_t)src[jj++];  
      }
    }
    //
    uint32_t usbCount=0;
    static int32_t src_buffer[NBUF_I2S];
    void AudioIF::update(void)
    {	
      audio_block_t *left, *right;
      //
      if(!getAudio(src_buffer)) return;
      if(fsamp != 44100) return;

      left  = allocate(); if (!left) return;
      right = allocate(); if (!right) {release(left); return;}
      usbCount++;
      //
      extract(left->data, right->data, src_buffer );
      
      transmit(left,0);
      transmit(right,1);
      release(left);
      release(right);
    }
  #endif
#endif
