/* microPAM 
 * Copyright (c) 2023/2024, Walter Zimmer
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
// AudioTrigger.h
// ensures Audio updates for USB streaming
//

#ifndef AUDIOTRIGGER_H
#define AUDIOTRIGGER_H

  #if defined(__IMXRT1062__)
    #include <Arduino.h>
    #if defined(AUDIO_INTERFACE)

      #include "AudioStream.h"

      class AudioTrigger : public AudioStream
      {
      public:
          AudioTrigger() : AudioStream(0, NULL) {init(); prio = 8;}
          void init(void);
          virtual void update(void) {;}
      private:
          static bool update_responsibility;
          static void m_isr(void);
          int prio;
      };
    #endif
  #endif
#endif
