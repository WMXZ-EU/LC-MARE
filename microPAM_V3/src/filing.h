/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
// microSD filing: WAV header, file/directory management, duty cycle, config.txt.
#ifndef FILING_H
#define FILING_H
#include "global.h"

  // 512-byte WAV header: RIFF/fmt chunks plus LIST/INFO metadata padded to one sector
  typedef struct {
      char    rId[4];                   //   4
      uint32_t rLen;                    //   8
      char    wId[4];                   //  12
      char    fId[4];                   //  16
      uint32_t fLen;                    //  20
      uint16_t nFormatTag;              //  22
      uint16_t nChannels;               //  24
      uint32_t nSamplesPerSec;          //  28
      uint32_t nAvgBytesPerSec;         //  32
      uint16_t nBlockAlign;             //  34
      uint16_t nBitsPerSamples;         //  36
      char    lId[4];                   //  40
      uint32_t lLen;                    //  44
      char    iId[4];                   //  48
      char    info[512-14*4];           // 504
      char    dId[4];                   // 508
      uint32_t dLen;                    // 512
  } HdrStruct;

  extern uint32_t diskBuffer[];         // one queue block

  uint16_t SD_init(void);
  void SD_stop(void);
  status_t logger(status_t status);

  int16_t loadConfigfromFile(void);
  int storeConfigToFile(void);       // 1 if written
  int configShow(void);

#endif
