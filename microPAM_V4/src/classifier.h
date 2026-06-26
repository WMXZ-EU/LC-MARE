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
  // 4 parallel VAEs, each receiving one quarter of the intensity spectrum.
  // Architecture per VAE: QSAMP ─ 32 ─ 2 ─ 32 ─ QSAMP
  #define VAE_NSAMP (NBUF_I2S / NCHAN_ACQ)
  #define VAE_NVAE   4
  #define VAE_QSAMP  (VAE_NSAMP / VAE_NVAE)   // input size per VAE
  #define VAE_H1     32
  #define VAE_LAT    2                          // latent dims per VAE
  #define VAE_LAT_TOTAL (VAE_NVAE * VAE_LAT)   // = 8, all latent means exported

  // All latent means: [vae0_mu0, vae0_mu1, vae1_mu0, vae1_mu1, ...]
  extern float    vae_mu[VAE_LAT_TOTAL];
  // Execution time of the last classifier ISR in microseconds.
  extern uint32_t classifier_exec_us;

  void classifier_init(void);
  void classifier_trigger(const float *D, int nsamp);  // call from dsp_apply
  int  classifier_apply(const float *D, int nsamp);    // called by ISR internally
#else
  // On RP targets the classifier is not compiled.
  #define classifier_exec_us 0u
#endif // MCU==T_4_1

#endif // CLASSIFIER_H
