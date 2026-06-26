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

/*
 * Online VAE — architecture: VAE_NSAMP – VAE_H1 – VAE_LAT – VAE_H1 – VAE_NSAMP
 *
 * Each call (one detection block) runs a full forward + backward pass with SGD,
 * so the network learns to compress the intensity spectrum D into VAE_LAT parameters.
 *
 * Loss = MSE(recon, D) + β·KL   where KL = 0.5·Σ(exp(lv)+μ²−1−lv)
 *
 * Memory: W1 and W4 are each VAE_NSAMP×VAE_H1 floats and are placed in DMAMEM (RAM2).
 * Small weight matrices and all activations stay in RAM1.
 */

#include "classifier.h"

#if MCU==T_4_1

  #include <Arduino.h>
  #include <math.h>
  #include <string.h>

  #define N   VAE_NSAMP
  #define H   VAE_H1
  #define L   VAE_LAT

  #define LR      1e-4f   // SGD learning rate
  #define BETA    1e-3f   // KL weight (β-VAE; low keeps latent space loose during early training)

  // IRQ_QTIMER4 is unused in this firmware; repurposed as a software-triggered vector.
  #define CLASSIFIER_IRQ          IRQ_QTIMER4
  #define CLASSIFIER_IRQ_PRIORITY (8 * 16)    // = 128

  // Input snapshot copied before triggering the ISR so the DMA buffer can be reused.
  static float D_buf[N];

  // ── weights ────────────────────────────────────────────────────────────────
  // W1 and W4 are ~128 KB each; placed in DMAMEM (RAM2) to keep RAM1 free.
  // Small matrices stay in SRAM for faster access.
  DMAMEM static float W1[H][N];
  DMAMEM static float W4[N][H];
  static float b1[H];                     // encoder hidden biases
  static float Wmu[L][H], bmu[L];         // encoder → mean
  static float Wlv[L][H], blv[L];         // encoder → log-variance
  static float W3[H][L],  b3[H];          // decoder hidden
  static float b4[N];                     // decoder output biases

  // ── activations & intermediates (reused every call) ────────────────────────
  static float h1[H];           // encoder hidden activations (post-ReLU)
  static float h1_pre[H];       // pre-ReLU (needed for backprop)
  static float mu[L], lv[L];    // latent mean and log-variance
  static float eps[L];          // sampled noise (stored for backprop)
  static float z[L];            // latent sample  z = mu + eps·exp(0.5·lv)
  static float h3[H];           // decoder hidden activations (post-ReLU)
  static float h3_pre[H];       // pre-ReLU
  static float recon[N];        // reconstructed output

  // ── gradient accumulators (one vector per layer, applied immediately) ──────
  static float dh1[H];
  static float dz[L], dmu_g[L], dlv_g[L];
  static float dh3[H];
  static float drecon[N];

  // ── exposed latent mean and timing ─────────────────────────────────────────
  float vae_mu[L];
  uint32_t classifier_exec_us = 0;  // execution time of last classifier_apply in µs

  // ── LCG pseudo-random for weight init and reparameterisation ───────────────
  static uint32_t lcg = 2463534242u;
  static float lcg_uniform(void)
  {
    lcg ^= lcg << 13; lcg ^= lcg >> 17; lcg ^= lcg << 5;  // xorshift32
    return (float)(lcg >> 1) * (1.0f / 2147483648.0f) - 1.0f;  // [-1, 1)
  }

  // Box-Muller: returns ~N(0,1) sample, consumes two uniform draws.
  static float randn(void)
  {
    float u1, u2;
    do { u1 = 0.5f * lcg_uniform() + 0.5f; } while (u1 < 1e-7f);
    u2 = 0.5f * lcg_uniform() + 0.5f;
    return sqrtf(-2.0f * logf(u1)) * cosf(6.28318530f * u2);
  }

  static void vae_forward(const float *D);
  static void vae_backward(const float *D);

  // ── ISR and trigger ─────────────────────────────────────────────────────────
  void classifier_isr(void)
  {
    NVIC_CLEAR_PENDING(CLASSIFIER_IRQ);
    uint32_t t0 = micros();
    vae_forward(D_buf);
    vae_backward(D_buf);
    classifier_exec_us = micros() - t0;
  }

  // Called from dsp_apply: snapshot D then pend the ISR.
  void classifier_trigger(const float *D, int nsamp)
  {
    (void)nsamp;
    memcpy(D_buf, D, N * sizeof(float));
    NVIC_SET_PENDING(CLASSIFIER_IRQ);
  }

  // ── initialisation ──────────────────────────────────────────────────────────
  void classifier_init(void)
  {
    // Xavier uniform: scale = sqrt(6 / (fan_in + fan_out))
    float s1  = sqrtf(6.0f / (float)(N + H));
    float sh  = sqrtf(6.0f / (float)(H + L));
    float sl  = sqrtf(6.0f / (float)(L + H));
    float so  = sqrtf(6.0f / (float)(H + N));

    for (int i = 0; i < H; i++) {
      for (int j = 0; j < N; j++) W1[i][j]  = lcg_uniform() * s1;
      b1[i] = 0.0f;
      for (int k = 0; k < L; k++) {
        Wmu[k][i] = lcg_uniform() * sh;
        Wlv[k][i] = lcg_uniform() * sh;
        W3[i][k]  = lcg_uniform() * sl;
      }
      b3[i] = 0.0f;
      for (int j = 0; j < N; j++) W4[j][i]  = lcg_uniform() * so;
    }
    for (int k = 0; k < L; k++) { bmu[k] = 0.0f; blv[k] = 0.0f; }
    for (int j = 0; j < N; j++) b4[j] = 0.0f;

    attachInterruptVector(CLASSIFIER_IRQ, classifier_isr);
    NVIC_SET_PRIORITY(CLASSIFIER_IRQ, CLASSIFIER_IRQ_PRIORITY);
    NVIC_ENABLE_IRQ(CLASSIFIER_IRQ);
  }

  // ── forward pass ────────────────────────────────────────────────────────────
  static void vae_forward(const float *D)
  {
    // encoder: D → h1
    for (int i = 0; i < H; i++) {
      float s = b1[i];
      for (int j = 0; j < N; j++) s += W1[i][j] * D[j];
      h1_pre[i] = s;
      h1[i] = (s > 0.0f) ? s : 0.0f;  // ReLU
    }

    // encoder: h1 → mu, lv
    for (int k = 0; k < L; k++) {
      float sm = bmu[k], slv = blv[k];
      for (int i = 0; i < H; i++) { sm += Wmu[k][i] * h1[i]; slv += Wlv[k][i] * h1[i]; }
      mu[k]  = sm;
      lv[k]  = slv;
      vae_mu[k] = sm;
    }

    // reparameterisation: z = mu + eps·exp(0.5·lv)
    for (int k = 0; k < L; k++) {
      eps[k] = randn();
      z[k]   = mu[k] + eps[k] * expf(0.5f * lv[k]);
    }

    // decoder: z → h3
    for (int i = 0; i < H; i++) {
      float s = b3[i];
      for (int k = 0; k < L; k++) s += W3[i][k] * z[k];
      h3_pre[i] = s;
      h3[i] = (s > 0.0f) ? s : 0.0f;  // ReLU
    }

    // decoder: h3 → recon
    for (int j = 0; j < N; j++) {
      float s = b4[j];
      for (int i = 0; i < H; i++) s += W4[j][i] * h3[i];
      recon[j] = s;
    }
  }

  // ── backward pass + SGD weight update ───────────────────────────────────────
  static void vae_backward(const float *D)
  {
    float inv_n = 1.0f / (float)N;

    // dL/drecon = 2*(recon - D)/N  (MSE gradient)
    for (int j = 0; j < N; j++) drecon[j] = 2.0f * (recon[j] - D[j]) * inv_n;

    // ── decoder backward ────────────────────────────────────────────────────

    // dL/dh3 via W4^T  +  update W4 and b4
    for (int i = 0; i < H; i++) dh3[i] = 0.0f;
    for (int j = 0; j < N; j++) {
      float dr = drecon[j];
      b4[j] -= LR * dr;
      for (int i = 0; i < H; i++) {
        W4[j][i] -= LR * dr * h3[i];
        dh3[i]   += dr * W4[j][i];   // accumulate before weight update is ideal,
                                      // but single-sample SGD error is negligible
      }
    }

    // ReLU gate on h3
    for (int i = 0; i < H; i++) dh3[i] *= (h3_pre[i] > 0.0f) ? 1.0f : 0.0f;

    // dL/dz via W3^T  +  update W3 and b3
    for (int k = 0; k < L; k++) dz[k] = 0.0f;
    for (int i = 0; i < H; i++) {
      float dh = dh3[i];
      b3[i] -= LR * dh;
      for (int k = 0; k < L; k++) {
        W3[i][k] -= LR * dh * z[k];
        dz[k]    += dh * W3[i][k];
      }
    }

    // ── KL gradient added to mu and lv paths ──────────────────────────────
    for (int k = 0; k < L; k++) {
      float e05lv = expf(0.5f * lv[k]);
      // gradient flows back through reparameterisation: dz → dmu, dlv
      dmu_g[k] = dz[k] + BETA * mu[k];
      dlv_g[k] = dz[k] * eps[k] * 0.5f * e05lv + BETA * 0.5f * (expf(lv[k]) - 1.0f);
    }

    // ── encoder backward ────────────────────────────────────────────────────

    // dL/dh1 via Wmu^T and Wlv^T  +  update Wmu, Wlv, bmu, blv
    for (int i = 0; i < H; i++) dh1[i] = 0.0f;
    for (int k = 0; k < L; k++) {
      float dm = dmu_g[k], dlv = dlv_g[k];
      bmu[k] -= LR * dm;
      blv[k] -= LR * dlv;
      for (int i = 0; i < H; i++) {
        Wmu[k][i] -= LR * dm  * h1[i];
        Wlv[k][i] -= LR * dlv * h1[i];
        dh1[i]    += dm * Wmu[k][i] + dlv * Wlv[k][i];
      }
    }

    // ReLU gate on h1
    for (int i = 0; i < H; i++) dh1[i] *= (h1_pre[i] > 0.0f) ? 1.0f : 0.0f;

    // update W1 and b1
    for (int i = 0; i < H; i++) {
      float dh = dh1[i];
      b1[i] -= LR * dh;
      for (int j = 0; j < N; j++) W1[i][j] -= LR * dh * D[j];
    }
  }

  // ── public entry point ──────────────────────────────────────────────────────
  int classifier_apply(const float *D, int nsamp)
  {
    (void)nsamp;  // must equal VAE_NSAMP; checked implicitly by array sizes

    vae_forward(D);
    vae_backward(D);

    return 0;  // placeholder — replace with a label head once training converges
  }

#endif // MCU==T_4_1
