/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
// Minimal register access on a TwoWire bus (shared by ADC and external RTC).
#ifndef I2C_H
#define I2C_H
#include <stdint.h>
#include <Wire.h>

  class i2c_class
  { TwoWire *wire;

    public:
    i2c_class(TwoWire *wire, uint32_t speed = 100'000);
    uint8_t exist(uint8_t addr);
    uint8_t read(uint8_t addr, uint8_t reg);
    uint8_t write(uint8_t addr, uint8_t reg, uint8_t val);
    uint8_t readData(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t ndat);
    uint8_t writeData(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t ndat);
  };

#endif
