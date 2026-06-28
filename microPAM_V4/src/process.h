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

#ifndef PROCESS_H
#define PROCESS_H

class Queue
{ int16_t head,tail;
  volatile int16_t busy;
  int32_t cnt;

  public:
  Queue() {reset();}

  void reset(void);
  int push(uint32_t *data, int ndat);
  // push_pair: writes data1+data2 contiguously only if both fit in the same
  // queue block.  If the current block has insufficient remaining space the
  // block is finalised and the pair is written at the start of the next one.
  // Returns 1 on success, 0 if the queue is full or the combined size exceeds
  // NBLOCK (i.e. can never fit in a single block).
  int push_pair(uint32_t *data1, int n1, uint32_t *data2, int n2);
  int pull(uint32_t *data);
  int available(void);
};

extern Queue queue;

void process_init(void);
void process(int32_t *buffer);

#if defined(USE_SYNTH_SIGNAL) && defined(__IMXRT1062__)
  void synth_signal_fill(float fsamp);
#endif

extern uint32_t acq_missed;
extern uint32_t acq_count;
extern uint32_t process_max_us;

#if MCU==T_4_1
  extern uint32_t dsp_isr_max_us;
  void dsp_init(void);
  void dsp_trigger(int32_t *acq_buffer);
  int32_t *spectrum_power(int32_t *buffer);
  int32_t *dsp_apply(int32_t *buffer);

  extern float Imax;
  extern float Dmax;
  extern float Dmean;
  extern float Dsnr;
  extern float Dpeak;
#else
  // On RP targets DSP is not compiled; provide zero constants so shared code
  // referencing these symbols compiles without changes and emits no linker symbols.
  static inline void dsp_init(void) {}
  static inline void dsp_trigger(int32_t *) {}
  static inline int32_t *spectrum_power(int32_t *b) { return b; }
  static inline int32_t *dsp_apply(int32_t *b) { return b; }
  #define Imax  0.0f
  #define Dmax  0.0f
  #define Dmean 0.0f
  #define Dsnr  0.0f
  #define Dpeak 0.0f
#endif

#endif // PROCESS_H
