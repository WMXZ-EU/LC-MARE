/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
#include "Arduino.h"
#include "i2c.h"

i2c_class::i2c_class(TwoWire *wire, uint32_t speed)
{ this->wire = wire;
  wire->begin();
  delay(100);
  wire->setClock(speed);
}

uint8_t i2c_class::exist(uint8_t addr)
{ wire->beginTransmission(addr);
  return (wire->endTransmission() == 0);
}

uint8_t i2c_class::read(uint8_t addr, uint8_t reg)
{ wire->beginTransmission(addr);
  wire->write(reg);
  if (wire->endTransmission(false) != 0) return 0;
  if (wire->requestFrom((int)addr, 1) < 1) return 0;
  return wire->read();
}

uint8_t i2c_class::write(uint8_t addr, uint8_t reg, uint8_t val)
{ wire->beginTransmission(addr);
  wire->write(reg);
  wire->write(val);
  return (wire->endTransmission() == 0);
}

uint8_t i2c_class::readData(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t ndat)
{ wire->beginTransmission(addr);
  wire->write(reg);
  if (wire->endTransmission(false) != 0) return 0;
  if (wire->requestFrom((int)addr, (int)ndat) < ndat) return 0;
  for(int ii = 0; ii < ndat; ii++) data[ii] = wire->read();
  return 1;
}

uint8_t i2c_class::writeData(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t ndat)
{ wire->beginTransmission(addr);
  wire->write(reg);
  for(int ii = 0; ii < ndat; ii++) wire->write(data[ii]);
  return (wire->endTransmission() == 0);
}
