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
//#include <string.h>

#include "Arduino.h"

#include "mConfig.h"
#include "mQueue.h"

  #ifndef NBUF_ACQ
    #define NBUF_ACQ 128
  #endif

  #ifndef MAXBUF
    #define MAXBUF 128      // Queue length
  #endif

#if 1
  volatile static int queue_busy=0;
  static uint32_t data_buffer[MAXBUF][NBUF_ACQ];
  volatile int head=0;
  volatile int tail=0;
  
  uint16_t __not_in_flash_func(getDataCount)(void) { int num = tail-head; return num<0 ? num+MAXBUF : num; }

  int __not_in_flash_func(queue_isBusy)(void) { return queue_busy; }

  uint16_t __not_in_flash_func(pushData)(uint32_t *data)
  {
    if ( (tail+1)%MAXBUF == head ) return 0;
    //while(busy); 
    queue_busy=1;
    for(int ii=0;ii<NBUF_ACQ;ii++)data_buffer[tail][ii]=data[ii];
    tail = (tail+1)%MAXBUF;
    queue_busy=0;
    return 1; // signal success.
  }
  
  uint16_t __not_in_flash_func(pullData)(uint32_t *data)
  {
    if ( head==tail ) return 0;
    //while(busy); 
    queue_busy=1;
    for(int ii=0;ii<NBUF_ACQ;ii++)data[ii]=data_buffer[head][ii];
    head = (head+1)%MAXBUF;
    queue_busy=0;
    return 1;
  }
#else 

static uint32_t data_buffer[MAXBUF*NBUF_ACQ];

    /**
     * @brief Data storage class
     * 
     */
    class Data
    {
        public:
            Data(uint32_t * data) 
            { /**
             * @brief Constructor
             * @param data is pointer to data store
             * 
             */
                data_buffer=data; front_=0; rear_= MAXBUF;
            }

            uint16_t push(uint32_t * src)
            { 
                /** 
                 * @brief push data to storage
                 * @param src is pointer to data block
                 */
                uint16_t f =front_ ;
                if(f == rear_) return 0;

                uint32_t *ptr= data_buffer+f*NBUF_ACQ;
                memcpy(ptr,src,NBUF_ACQ*4);

                if(++f==MAXBUF) f=0;
                front_ = f;
                
                return 1;
            }

            uint16_t pull(uint32_t * dst)
            {   
                /** 
                 * @brief pull data from storage
                 * @param dst is pointer to data blocks
                 */
                uint16_t r = rear_ +1;
                if(r >= MAXBUF) r=0;
                if(r == front_) return 0;
                
                uint32_t *ptr= data_buffer + r*NBUF_ACQ;
                memcpy(dst,ptr,NBUF_ACQ*4);

                rear_ = r;
                return 1;
            }

            uint16_t pull(uint32_t * dst, uint32_t ndbl)
            {   
                /** 
                 * @brief pull data from storage
                 * @param dst is pointer to data blocks
                 * @param ndbl is number of data blocks
                 */
                uint16_t r = rear_ +1;
                if(r >= MAXBUF) r=0;
                if(r/ndbl == front_/ndbl) return 0;
                
                uint32_t *ptr= data_buffer + r*NBUF_ACQ;
                memcpy(dst,ptr,ndbl*NBUF_ACQ*4);

                r+=(ndbl-1);
                rear_ = r;
                return 1;
            }

            uint16_t getCount () 
            {  
                /**
                 * @brief get number of data blocks in storage
                 * 
                 */
                if(front_ > rear_) return (front_ - rear_); 
                return (front_+ MAXBUF -rear_); 
            }

    private:    
        uint16_t front_, rear_;
        uint32_t *data_buffer;
    };

    Data rawData(data_buffer);

    uint16_t getDataCount () { return rawData.getCount(); }
    uint16_t pushData(uint32_t * src){ return rawData.push(src);}

    uint16_t pullData(uint32_t * dst) {return rawData.pull(dst);}
    uint16_t pullData(uint32_t * dst, uint32_t ndbl) {return rawData.pull(dst,ndbl);}
#endif