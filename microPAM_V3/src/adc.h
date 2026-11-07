/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
// TLV320ADC6140 control (power, reset, I2C configuration).
#ifndef ADC_H
#define ADC_H
#include <stdint.h>

  void adc_init(void);
  void adc_exit(void);
  void setAGain(uint32_t again_dB);

#endif
