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
  #include <SdFat.h>
  #include "process.h"   // queue, acq_missed

  extern SdFs      sd;         // defined in filing.cxx
  extern uint16_t  have_disk;  // defined in filing.cxx

  #define VAE_MODEL_FILE  "/VAE_model.dat"
  #define VAE_MODEL_MAGIC 0x56414531u  // "VAE1"

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
  static float Dsnr_buf = 0.0f;   // Dsnr captured at trigger time
  // Full intensity matrix snapshot (3*NSAMP floats) before scale3 is applied.
  static float I_buf[3 * N];

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

  // ── per-VAE background EMA (noise frames only) ──────────────────────────────
  static float mu_bg[4][L];      // background latent mean per VAE
  static float recon_bg[4];      // background reconstruction error per VAE
  static bool  bg_seeded[4];     // whether the EMA has been seeded

  // ── exported globals ─────────────────────────────────────────────────────────
  float    vae_mu[VAE_LAT_TOTAL];           // current frame latent means
  float    vae_detect[VAE_NVAE];            // per-VAE detection score
  float    vae_mu_signal[VAE_LAT_TOTAL];    // accumulated means on signal frames
  uint32_t vae_mu_signal_count = 0;
  uint32_t classifier_exec_us = 0;
  uint32_t classifier_isr_max_us = 0;

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
    bool signal_frame = (Dsnr_buf >= DETECT_THR);

    for (int v = 0; v < 4; v++) {
      const float *D_q = D_buf + v * Q;
      vae_forward(v, D_q);

      // per-VAE reconstruction error (MSE)
      float err_v = 0.0f;
      for (int j = 0; j < Q; j++) { float e = recon[j] - D_q[j]; err_v += e * e; }
      err_v /= (float)Q;

      if (!signal_frame) vae_backward(v, D_q);

      // background EMA — updated on noise frames only
      if (!signal_frame) {
        if (!bg_seeded[v]) {
          for (int k = 0; k < L; k++) mu_bg[v][k] = vae_mu[v * L + k];
          recon_bg[v] = err_v;
          bg_seeded[v] = true;
        } else {
          for (int k = 0; k < L; k++)
            mu_bg[v][k] += DETECT_ALPHA * (vae_mu[v * L + k] - mu_bg[v][k]);
          recon_bg[v] += DETECT_ALPHA * (err_v - recon_bg[v]);
        }
      }

      // per-VAE detection score: ||mu - mu_bg|| * (err / err_bg)
      if (bg_seeded[v]) {
        float mu_dev = 0.0f;
        for (int k = 0; k < L; k++) {
          float d = vae_mu[v * L + k] - mu_bg[v][k];
          mu_dev += d * d;
        }
        mu_dev = sqrtf(mu_dev);
        float bg = (recon_bg[v] > 1e-12f) ? recon_bg[v] : 1e-12f;
        vae_detect[v] = mu_dev * (err_v / bg);
      } else {
        vae_detect[v] = 0.0f;
      }
    }

    if (signal_frame) {
      for (int k = 0; k < VAE_LAT_TOTAL; k++) vae_mu_signal[k] += vae_mu[k];
      vae_mu_signal_count++;
    }

    classifier_exec_us = micros() - t0;
    if (classifier_exec_us > classifier_isr_max_us) classifier_isr_max_us = classifier_exec_us;

    #if PROC_MODE==4
    {
      // Result vector layout:
      //  Header (24 x uint32 = 96 bytes) — pushed every frame:
      //    [0]     magic 0x55555555
      //    [1]     millis()
      //    [2]     signal_flag (0 or 1)
      //    [3]     detection_excess = Dsnr - DETECT_THR  (float bits)
      //    [4..7]  vae_detect[4]                         (float bits)
      //    [8..23] vae_mu[16]                            (float bits)
      //  Signal tail (3*NSAMP x uint32 = 6144 bytes) — pushed only when signal_flag==1:
      //    I[3*NSAMP] interleaved as I[comp + 3*bin], comp={x,y,z} (float bits)
      uint32_t result[27];
      float detection_excess = Dsnr_buf - DETECT_THR;
      result[0] = 0x55555555u;
      result[1] = millis();
      result[2] = signal_frame ? 1u : 0u;
      memcpy(&result[3], &detection_excess, 4);
      memcpy(&result[4], vae_detect, VAE_NVAE * 4);
      memcpy(&result[8], vae_mu,     VAE_LAT_TOTAL * 4);
      if (signal_frame) {
        // Atomic pair-push: header + I matrix land in the same queue block.
        if (!queue.push_pair(result, 24, (uint32_t *)I_buf, 3 * N)) acq_missed++;
      } else {
        if (!queue.push(result, 24)) acq_missed++;
      }
    }
    #endif
  }

  void classifier_trigger(const float *D, int nsamp, float dsnr,
                           const float *I, int ni)
  {
    (void)nsamp;
    memcpy(D_buf, D, N * sizeof(float));
    Dsnr_buf = dsnr;

    // Snapshot full intensity matrix before scale3 is applied.
    // Layout: I[comp + 3*bin], ni = 3*NSAMP, comp ∈ {0=x, 1=y, 2=z}
    memcpy(I_buf, I, (size_t)ni * sizeof(float));

    NVIC_SET_PENDING(CLASSIFIER_IRQ);
  }

  // ── model persistence ────────────────────────────────────────────────────────
  void classifier_load(void)
  {
    // Always start with a clean random initialisation.
    memset(mu_bg,    0, sizeof(mu_bg));
    memset(recon_bg, 0, sizeof(recon_bg));
    memset(bg_seeded,0, sizeof(bg_seeded));
    memset(vae_detect,0, sizeof(vae_detect));

    float s1 = sqrtf(6.0f / (float)(Q + H));
    float sh = sqrtf(6.0f / (float)(H + L));
    float sl = sqrtf(6.0f / (float)(L + H));
    float so = sqrtf(6.0f / (float)(H + Q));
    for (int v = 0; v < 4; v++) {
      for (int i = 0; i < H; i++) {
        for (int j = 0; j < Q; j++) W1[v][i][j] = lcg_uniform() * s1;
        b1[v][i] = 0.0f;
        for (int k = 0; k < L; k++) {
          Wmu[v][k][i] = lcg_uniform() * sh;
          Wlv[v][k][i] = lcg_uniform() * sh;
          W3[v][i][k]  = lcg_uniform() * sl;
        }
        b3[v][i] = 0.0f;
        for (int j = 0; j < Q; j++) W4[v][j][i] = lcg_uniform() * so;
      }
      for (int k = 0; k < L; k++) { bmu[v][k] = 0.0f; blv[v][k] = 0.0f; }
      for (int j = 0; j < Q; j++) b4[v][j] = 0.0f;
    }

    // Try to load saved weights from SD card.
    if (!have_disk) { Serial.println("VAE: no SD, random init"); return; }
    FsFile f = sd.open(VAE_MODEL_FILE, FILE_READ);
    if (!f) { Serial.println("VAE: no model file, random init"); return; }

    uint32_t hdr[2];
    if (f.read(hdr, sizeof(hdr)) != (int)sizeof(hdr) || hdr[0] != VAE_MODEL_MAGIC) {
      f.close();
      Serial.println("VAE: bad model file, random init");
      return;
    }
    f.read(W1,       sizeof(W1));
    f.read(W4,       sizeof(W4));
    f.read(b1,       sizeof(b1));
    f.read(Wmu,      sizeof(Wmu));
    f.read(bmu,      sizeof(bmu));
    f.read(Wlv,      sizeof(Wlv));
    f.read(blv,      sizeof(blv));
    f.read(W3,       sizeof(W3));
    f.read(b3,       sizeof(b3));
    f.read(b4,       sizeof(b4));
    f.read(mu_bg,    sizeof(mu_bg));
    f.read(recon_bg, sizeof(recon_bg));
    f.read(bg_seeded,sizeof(bg_seeded));
    f.close();
    Serial.println("VAE: model loaded from SD");
  }

  void classifier_save(const char *datestring)
  {
    if (!have_disk) { Serial.println("VAE: no SD, model not saved"); return; }
    // Rename existing file into /VAE_backup/ before overwriting.
    if (sd.exists(VAE_MODEL_FILE)) {
      if (!sd.exists("/VAE_backup")) sd.mkdir("/VAE_backup");
      char backup[48];
      char ts[14];
      strncpy(ts, datestring, 13); ts[13] = '\0';
      snprintf(backup, sizeof(backup), "/VAE_backup/VAE_model_%s.dat", ts);
      sd.rename(VAE_MODEL_FILE, backup);
    }

    FsFile f = sd.open(VAE_MODEL_FILE, FILE_WRITE);
    if (!f) { Serial.println("VAE: cannot save model"); return; }

    uint32_t hdr[2] = {VAE_MODEL_MAGIC, 1u};
    f.write(hdr,      sizeof(hdr));
    f.write(W1,       sizeof(W1));
    f.write(W4,       sizeof(W4));
    f.write(b1,       sizeof(b1));
    f.write(Wmu,      sizeof(Wmu));
    f.write(bmu,      sizeof(bmu));
    f.write(Wlv,      sizeof(Wlv));
    f.write(blv,      sizeof(blv));
    f.write(W3,       sizeof(W3));
    f.write(b3,       sizeof(b3));
    f.write(b4,       sizeof(b4));
    f.write(mu_bg,    sizeof(mu_bg));
    f.write(recon_bg, sizeof(recon_bg));
    f.write(bg_seeded,sizeof(bg_seeded));
    f.close();
    Serial.println("VAE: model saved");
  }

  // ── initialisation ──────────────────────────────────────────────────────────
  void classifier_init(void)
  {
    memset(I_buf,     0, sizeof(I_buf));
    classifier_load();   // random init + load from SD if available

    attachInterruptVector(CLASSIFIER_IRQ, classifier_isr);
    NVIC_SET_PRIORITY(CLASSIFIER_IRQ, CLASSIFIER_IRQ_PRIORITY);
    NVIC_ENABLE_IRQ(CLASSIFIER_IRQ);
  }

  // ── public entry point (called directly when not using ISR path) ─────────────
  int classifier_apply(const float *D, int nsamp, bool do_train)
  {
    (void)nsamp;
    for (int v = 0; v < 4; v++) {
      const float *D_q = D + v * Q;
      vae_forward(v, D_q);
      if (do_train) vae_backward(v, D_q);
    }
    if (!do_train) {
      for (int k = 0; k < VAE_LAT_TOTAL; k++) vae_mu_signal[k] += vae_mu[k];
      vae_mu_signal_count++;
    }
    return 0;
  }

#endif // MCU==T_4_1
