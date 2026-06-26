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
 * 4 parallel online VAEs — each processes one quarter of the intensity spectrum D.
 *
 * Architecture per VAE:  QSAMP – H – LAT – H – QSAMP
 *   QSAMP = VAE_NSAMP / 4,  H = 32,  LAT = 2
 *
 * Each call runs a full forward + backward (SGD) pass for all four VAEs in sequence.
 * Loss = MSE(recon, D_quarter) + β·KL
 *
 * Memory layout:
 *   W1[4][H][Q] and W4[4][Q][H] are placed in DMAMEM (total same size as the
 *   original single-VAE W1/W4 at full N width).
 *   All other weights and activations stay in RAM1.
 *   Activation scratch buffers are reused across the four VAEs (sequential execution).
 */

#include "classifier.h"

#if MCU==T_4_1

  #include <Arduino.h>
  #include <math.h>
  #include <string.h>
  #include "process.h"   // queue, acq_missed

  #define N   VAE_NSAMP
  #define Q   VAE_QSAMP
  #define H   VAE_H1
  #define L   VAE_LAT

  #define LR      1e-4f
  #define BETA    1e-3f

  #define CLASSIFIER_IRQ          IRQ_QTIMER4
  #define CLASSIFIER_IRQ_PRIORITY (8 * 16)

  // Input snapshot (full N samples); each VAE reads its Q-sample slice.
  static float D_buf[N];

  // ── per-VAE weights ─────────────────────────────────────────────────────────
  // Large encoder/decoder matrices in DMAMEM: 4 * H * Q = H * N total (same as before).
  DMAMEM static float W1[4][H][Q];
  DMAMEM static float W4[4][Q][H];

  // Small matrices in SRAM
  static float b1 [4][H];
  static float Wmu[4][L][H], bmu[4][L];
  static float Wlv[4][L][H], blv[4][L];
  static float W3 [4][H][L], b3[4][H];
  static float b4 [4][Q];

  // ── shared activation scratch (reused for each VAE in sequence) ─────────────
  static float h1[H],    h1_pre[H];
  static float mu_s[L],  lv_s[L], eps_s[L], z_s[L];
  static float h3[H],    h3_pre[H];
  static float recon[Q];

  static float dh1[H];
  static float dz[L], dmu_g[L], dlv_g[L];
  static float dh3[H];
  static float drecon[Q];

  // ── exported latent means and timing ────────────────────────────────────────
  float    vae_mu[VAE_LAT_TOTAL];    // [vae0_mu0, vae0_mu1, vae1_mu0, ...]
  uint32_t classifier_exec_us = 0;

  // ── LCG / Box-Muller ────────────────────────────────────────────────────────
  static uint32_t lcg = 2463534242u;
  static float lcg_uniform(void)
  {
    lcg ^= lcg << 13; lcg ^= lcg >> 17; lcg ^= lcg << 5;
    return (float)(lcg >> 1) * (1.0f / 2147483648.0f) - 1.0f;
  }
  static float randn(void)
  {
    float u1, u2;
    do { u1 = 0.5f * lcg_uniform() + 0.5f; } while (u1 < 1e-7f);
    u2 = 0.5f * lcg_uniform() + 0.5f;
    return sqrtf(-2.0f * logf(u1)) * cosf(6.28318530f * u2);
  }

  // ── single-VAE forward pass (index v, input slice D_q of length Q) ──────────
  static void vae_forward(int v, const float *D_q)
  {
    // encoder: D_q → h1
    for (int i = 0; i < H; i++) {
      float s = b1[v][i];
      for (int j = 0; j < Q; j++) s += W1[v][i][j] * D_q[j];
      h1_pre[i] = s;
      h1[i] = (s > 0.0f) ? s : 0.0f;
    }

    // encoder: h1 → mu, lv
    for (int k = 0; k < L; k++) {
      float sm = bmu[v][k], slv = blv[v][k];
      for (int i = 0; i < H; i++) { sm += Wmu[v][k][i] * h1[i]; slv += Wlv[v][k][i] * h1[i]; }
      mu_s[k] = sm;
      lv_s[k] = slv;
      vae_mu[v * L + k] = sm;
    }

    // reparameterisation: z = mu + eps·exp(0.5·lv)
    for (int k = 0; k < L; k++) {
      eps_s[k] = randn();
      z_s[k]   = mu_s[k] + eps_s[k] * expf(0.5f * lv_s[k]);
    }

    // decoder: z → h3
    for (int i = 0; i < H; i++) {
      float s = b3[v][i];
      for (int k = 0; k < L; k++) s += W3[v][i][k] * z_s[k];
      h3_pre[i] = s;
      h3[i] = (s > 0.0f) ? s : 0.0f;
    }

    // decoder: h3 → recon
    for (int j = 0; j < Q; j++) {
      float s = b4[v][j];
      for (int i = 0; i < H; i++) s += W4[v][j][i] * h3[i];
      recon[j] = s;
    }
  }

  // ── single-VAE backward + SGD update ────────────────────────────────────────
  static void vae_backward(int v, const float *D_q)
  {
    float inv_q = 1.0f / (float)Q;

    for (int j = 0; j < Q; j++) drecon[j] = 2.0f * (recon[j] - D_q[j]) * inv_q;

    // decoder backward: W4, b4
    for (int i = 0; i < H; i++) dh3[i] = 0.0f;
    for (int j = 0; j < Q; j++) {
      float dr = drecon[j];
      b4[v][j] -= LR * dr;
      for (int i = 0; i < H; i++) {
        W4[v][j][i] -= LR * dr * h3[i];
        dh3[i]      += dr * W4[v][j][i];
      }
    }

    for (int i = 0; i < H; i++) dh3[i] *= (h3_pre[i] > 0.0f) ? 1.0f : 0.0f;

    // decoder backward: W3, b3
    for (int k = 0; k < L; k++) dz[k] = 0.0f;
    for (int i = 0; i < H; i++) {
      float dh = dh3[i];
      b3[v][i] -= LR * dh;
      for (int k = 0; k < L; k++) {
        W3[v][i][k] -= LR * dh * z_s[k];
        dz[k]       += dh * W3[v][i][k];
      }
    }

    // KL gradient
    for (int k = 0; k < L; k++) {
      float e05lv = expf(0.5f * lv_s[k]);
      dmu_g[k] = dz[k] + BETA * mu_s[k];
      dlv_g[k] = dz[k] * eps_s[k] * 0.5f * e05lv + BETA * 0.5f * (expf(lv_s[k]) - 1.0f);
    }

    // encoder backward: Wmu, Wlv, bmu, blv
    for (int i = 0; i < H; i++) dh1[i] = 0.0f;
    for (int k = 0; k < L; k++) {
      float dm = dmu_g[k], dlv = dlv_g[k];
      bmu[v][k] -= LR * dm;
      blv[v][k] -= LR * dlv;
      for (int i = 0; i < H; i++) {
        Wmu[v][k][i] -= LR * dm  * h1[i];
        Wlv[v][k][i] -= LR * dlv * h1[i];
        dh1[i]       += dm * Wmu[v][k][i] + dlv * Wlv[v][k][i];
      }
    }

    for (int i = 0; i < H; i++) dh1[i] *= (h1_pre[i] > 0.0f) ? 1.0f : 0.0f;

    // encoder backward: W1, b1
    for (int i = 0; i < H; i++) {
      float dh = dh1[i];
      b1[v][i] -= LR * dh;
      for (int j = 0; j < Q; j++) W1[v][i][j] -= LR * dh * D_q[j];
    }
  }

  // ── ISR ─────────────────────────────────────────────────────────────────────
  void classifier_isr(void)
  {
    NVIC_CLEAR_PENDING(CLASSIFIER_IRQ);
    uint32_t t0 = micros();
    for (int v = 0; v < 4; v++) {
      const float *D_q = D_buf + v * Q;
      vae_forward(v, D_q);
      vae_backward(v, D_q);
    }
    classifier_exec_us = micros() - t0;
    #if PROC_MODE==3
      // Push the 8 latent means (float bits reinterpreted as uint32_t) onto the queue.
      if (!queue.push((uint32_t *)vae_mu, VAE_LAT_TOTAL)) acq_missed++;
    #endif
  }

  void classifier_trigger(const float *D, int nsamp)
  {
    (void)nsamp;
    memcpy(D_buf, D, N * sizeof(float));
    NVIC_SET_PENDING(CLASSIFIER_IRQ);
  }

  // ── initialisation ──────────────────────────────────────────────────────────
  void classifier_init(void)
  {
    float s1 = sqrtf(6.0f / (float)(Q + H));
    float sh = sqrtf(6.0f / (float)(H + L));
    float sl = sqrtf(6.0f / (float)(L + H));
    float so = sqrtf(6.0f / (float)(H + Q));

    for (int v = 0; v < 4; v++) {
      for (int i = 0; i < H; i++) {
        for (int j = 0; j < Q; j++) W1[v][i][j]  = lcg_uniform() * s1;
        b1[v][i] = 0.0f;
        for (int k = 0; k < L; k++) {
          Wmu[v][k][i] = lcg_uniform() * sh;
          Wlv[v][k][i] = lcg_uniform() * sh;
          W3[v][i][k]  = lcg_uniform() * sl;
        }
        b3[v][i] = 0.0f;
        for (int j = 0; j < Q; j++) W4[v][j][i]  = lcg_uniform() * so;
      }
      for (int k = 0; k < L; k++) { bmu[v][k] = 0.0f; blv[v][k] = 0.0f; }
      for (int j = 0; j < Q; j++) b4[v][j] = 0.0f;
    }

    attachInterruptVector(CLASSIFIER_IRQ, classifier_isr);
    NVIC_SET_PRIORITY(CLASSIFIER_IRQ, CLASSIFIER_IRQ_PRIORITY);
    NVIC_ENABLE_IRQ(CLASSIFIER_IRQ);
  }

  // ── public entry point (called directly when not using ISR path) ─────────────
  int classifier_apply(const float *D, int nsamp)
  {
    (void)nsamp;
    for (int v = 0; v < 4; v++) {
      const float *D_q = D + v * Q;
      vae_forward(v, D_q);
      vae_backward(v, D_q);
    }
    return 0;
  }

#endif // MCU==T_4_1
