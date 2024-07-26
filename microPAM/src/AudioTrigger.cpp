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
// AudioTrigger.cpp
// ensures Audio updates for USB streaming
//
#if defined(__IMXRT1062__) 
  #include <Arduino.h>

  #if defined(AUDIO_INTERFACE)
    #include <IntervalTimer.h>
    #include "mConfig.h"
    #include "mAudioTrigger.h"

    bool AudioTrigger::update_responsibility = false;

    IntervalTimer t1;
    const int period = (int) (128.0f/44100*1000000.0f);

    void AudioTrigger::init(void)
    {	// check with AudioStream if we are responsable for updates
      update_responsibility = update_setup();  

      t1.begin(&m_isr,period);
      t1.priority(prio*16);
    }

    void AudioTrigger::m_isr(void)
    {
      if (update_responsibility) AudioStream::update_all();
    }
  #endif
#endif