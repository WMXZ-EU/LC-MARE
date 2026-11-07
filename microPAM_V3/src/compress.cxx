/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
// Lossless compression of one acquisition chunk (PROC_MODE 1).
//
// The samples are differenced along time (per channel), and all differences are packed
// with the smallest common bit width nb that holds them. Output frame (32-bit words):
//
//   [0]           COMP_MAGIC        sync word "COMP" (0x434F4D50)
//   [1]           unix seconds      rtc_get_unix()
//   [2]           micros()          free-running microsecond counter
//   [3]           nb                bits per packed difference
//   [4]           nd                number of packed words
//   [5..5+NCH-1]  first sample of each channel
//   [5+NCH..]     nd words of packed differences (NBUF_ACQ values, MSB first)
//
// This is the microPAM_V5 record layout, so the V5 reader decodes V3 files.
//
// The frame is zero padded to a multiple of 128 words (512 bytes).
//
#include <stdint.h>
#include "global.h"
#include "compress.h"
#include "rtc.h"

static int32_t tempData[NBUF_ACQ];

static int32_t __not_in_flash_func(encodeBlock)(uint32_t *uout, const uint32_t *uinp, int32_t ndata, int32_t nb)
{ int nx = MBIT;
  int kk = 0;
  for (int ii = 0; ii < ndata; ii++)
  { nx -= nb;
    if(nx > 0)
    { uout[kk] |= uinp[ii] << nx;
    }
    else if(nx == 0)
    { uout[kk++] |= uinp[ii];
      nx = MBIT;
    }
    else    // value straddles two output words
    { uout[kk++] |= uinp[ii] >> (-nx);
      nx += MBIT;
      uout[kk] = uinp[ii] << nx;
    }
  }
  return (nx == MBIT) ? kk : kk+1;
}

static int32_t __not_in_flash_func(encodeData)(uint32_t *out, int32_t *inp, int ndat, int nch)
{
  // differences along time; first sample of each channel kept separately
  for(int ii = 0; ii < nch; ii++) tempData[ii] = inp[ii];
  for(int ii = nch; ii < ndat; ii++) tempData[ii] = inp[ii] - inp[ii-nch];

  // input buffer is reused as output buffer
  for(int ii = 0; ii < ndat; ii++) inp[ii] = 0;

  // absolute maximum of differences
  uint32_t amax = 0;
  for(int ii = nch; ii < ndat; ii++)
  { int32_t tmp = tempData[ii];
    if(tmp < 0) tmp = -tmp;
    if((uint32_t)tmp > amax) amax = tmp;
  }

  // bit width incl. sign bit (at least 3)
  uint32_t nb;
  for(nb = 2; nb <= 24; nb++) if(amax < (1u << nb)) break;
  nb++;
  #if NB_MIN > 0
    if(nb < NB_MIN) nb = NB_MIN;                  // test only: fixed minimum data volume
  #endif
  uint32_t mask = (1u << nb) - 1;

  int kk = 0;
  out[kk++] = COMP_MAGIC;
  out[kk++] = rtc_get_unix();
  out[kk++] = micros();
  out[kk++] = nb;
  out[kk++] = 0;                                  // nd, filled in below
  for(int ii = 0; ii < nch; ii++) { out[kk+ii] = tempData[ii]; tempData[ii] = 0; }

  uint32_t *utmp = (uint32_t *) tempData;
  for(int ii = nch; ii < ndat; ii++) utmp[ii] &= mask;

  int32_t nd = encodeBlock(&out[kk+nch], utmp, ndat, nb);
  out[kk-1] = nd;
  return kk + nch + nd;
}

// compress buffer in place; returns frame length in words (multiple of 128)
int32_t __not_in_flash_func(compressData)(int32_t *buffer)
{
  // drop zero padding and lowest noise bits
  for(int ii = 0; ii < NBUF_ACQ; ii++) buffer[ii] = buffer[ii] >> SHIFT;

  uint32_t *outData = (uint32_t *) buffer;
  int32_t kk = encodeData(outData, buffer, NBUF_ACQ, NCH);

  int32_t nbuf = ((kk + 127)/128)*128;
  for(; kk < nbuf; kk++) outData[kk] = 0;
  return nbuf;
}
