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

#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#include "global.h"
#include <cstdint>

#if MCU==T_4_1
  // VAE architecture: NSAMP ─ 32 ─ 2 ─ 32 ─ NSAMP
  // Input size matches the per-block intensity vector D passed from detection_apply.
  #define VAE_NSAMP (NBUF_I2S / NCHAN_ACQ)
  #define VAE_H1   32
  #define VAE_LAT   4

  // Latent mean after the last forward pass.
  extern float    vae_mu[VAE_LAT];
  // Execution time of the last classifier_apply call in microseconds.
  extern uint32_t classifier_exec_us;

  void classifier_init(void);
  void classifier_trigger(const float *D, int nsamp);  // call from dsp_apply
  int  classifier_apply(const float *D, int nsamp);    // called by ISR internally
#else
  // On RP targets the classifier is not compiled; provide a zero constant so
  // any code referencing classifier_exec_us still compiles without changes.
  #define classifier_exec_us 0u
#endif // MCU==T_4_1

#endif // CLASSIFIER_H
