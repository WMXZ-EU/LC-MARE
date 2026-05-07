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

#include "Arduino.h"

#include "global.h"
#include "Queue.h"

  #ifndef MAX_QUEUE
    #define MAX_QUEUE 12      // Queue length
  #endif

  #define NBLOCK MD*BLOCK_SIZE

  #define INC(x) ((x+1)%MAX_QUEUE)

  volatile int queue_busy=0;
  uint32_t data_buffer[MAX_QUEUE][NBLOCK];
  volatile int head=0;  // head of stored data (pushing data will increase head )
  volatile int tail=0;  // tail of stored data (pulling data will increase tail)
  enum QueueStatus_t  {queueOK, queueEmpty, queueFull};
  volatile QueueStatus_t queueStatus=queueEmpty;

  uint16_t __not_in_flash_func(getQueueCount)(void) 
  { if(queueStatus==queueEmpty) return 0;
    if(queueStatus==queueFull) return MAX_QUEUE;
    return (head-tail+MAX_QUEUE) % MAX_QUEUE;
    //int num = head-tail; 
    //return num<0 ? num+MAX_QUEUE : num; 
  }

  int __not_in_flash_func(queue_isBusy)(void) { return queue_busy; }

  uint16_t __not_in_flash_func(pushQueue)(uint32_t *data)
  { 
    if ( queueStatus == queueFull ) return 0; // full queue

    queue_busy=1;
    memcpy(data_buffer[head],data,4*NBLOCK);

    head=INC(head);
    queueStatus = (head==tail)? queueFull: queueOK;
    queue_busy=0;
    return 1; // signal success.
  }

  uint16_t __not_in_flash_func(pushQueue_c)(uint32_t *data, int ndat)
  { static uint32_t nbuf=0;
    
    if ( queueStatus == queueFull ) return 0; // full queue

    if(nbuf+ndat<NBLOCK) 
    {
      queue_busy=1;
      memcpy(&data_buffer[head][nbuf],data,4*ndat);
      nbuf += ndat;
      queue_busy=0;
      return 1; // signal success.
    }
    else  // buffer is filled
    { for (int ii=nbuf; ii<NBLOCK;ii++) data_buffer[head][ii]=0; 
      data_buffer[head][NBLOCK-1]=nbuf;
      nbuf=0;
      head=INC(head);
      queueStatus = (head==tail)? queueFull: queueOK;
      if ( queueStatus == queueFull ) return 1; // full queue but prevous filled
      //
      queue_busy=1;
      memcpy(&data_buffer[head][0],data,4*ndat);
      nbuf += ndat;
      queue_busy=0;
      return 1;
    }

  }
  
  uint16_t __not_in_flash_func(pullQueue)(uint32_t *data)
  {
    if ( queueStatus==queueEmpty) return 0; // empty queue
    //while(busy); 
    queue_busy=1;
    memcpy(data,data_buffer[tail],4*NBLOCK);

    tail=INC(tail);
    queueStatus=(tail==head)? queueEmpty : queueOK;
    queue_busy=0;
    return 1;
  }
