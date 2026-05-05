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
#include "mQueue.h"

  #ifndef MAX_QUEUE
    #define MAX_QUEUE 12      // Queue length
  #endif

  #define NBLOCK BLOCK_SIZE

  #define INC(x) ((x+1)%MAX_QUEUE)

  volatile int queue_busy=0;
  uint32_t data_buffer[MAX_QUEUE][NBLOCK];
  volatile int head=0;
  volatile int tail=0;
  
  uint16_t __not_in_flash_func(getDataCount)(void) { int num = tail-head; return num<0 ? num+MAX_QUEUE : num; }

  int __not_in_flash_func(queue_isBusy)(void) { return queue_busy; }

  uint16_t __not_in_flash_func(pushData)(uint32_t *data)
  {
    if ( INC(tail) == head ) return 0;
    //while(busy); 
    queue_busy=1;
    memcpy(data_buffer[tail],data,4*NBLOCK);

    tail=INC(tail);
    queue_busy=0;
    return 1; // signal success.
  }
  
  uint16_t __not_in_flash_func(pullData)(uint32_t *data)
  {
    if ( head==tail ) return 0;
    //while(busy); 
    queue_busy=1;
    memcpy(data,data_buffer[head],4*NBLOCK);

    head=INC(head);
    queue_busy=0;
    return 1;
  }
