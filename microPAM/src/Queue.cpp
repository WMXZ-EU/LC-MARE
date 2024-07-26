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
//#include <string.h>

#include "Arduino.h"

#include "Config.h"
#include "Queue.h"

  #ifndef NBUF_ACQ
    #define NBUF_ACQ 128    // single channel
  #endif

  #ifndef MAXBUF
    #define MAXBUF 128      // Queue length
  #endif

  volatile static int queue_busy=0;
  #if defined(__IMXRT1062__)
    EXTMEM 
  #endif
  static uint32_t data_buffer[MAXBUF][NBUF_ACQ];
  volatile static int head=0;
  volatile static int tail=0;
  
  uint16_t __not_in_flash_func(getDataCount)(void) { int num = tail-head; return num<0 ? num+MAXBUF : num; }
  int __not_in_flash_func(queue_isBusy)(void) { return queue_busy; }

  uint16_t __not_in_flash_func(pushData)(uint32_t *data)
  {
//    while(queue_busy); 
    queue_busy=1;
    if ( (tail+1)%MAXBUF == head ) {queue_busy=0; return 0;} // signal full
    memcpy(data_buffer[tail],data,4*NBUF_ACQ);
    tail = (tail+1)%MAXBUF;
    queue_busy=0;
    return 1; // signal success.
  }
  
  uint16_t __not_in_flash_func(pullData)(uint32_t *data)
  {
//    while(queue_busy); 
    queue_busy=1;
    if ( head==tail ) {queue_busy=0; return 0;} // signal empty
    memcpy(data,data_buffer[head],4*NBUF_ACQ);
    head = (head+1)%MAXBUF;
    queue_busy=0;
    return 1; // signal success.
  }
