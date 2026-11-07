/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
#include "Arduino.h"
#include "Wire.h"
#include "global.h"
#include "adc.h"
#include "i2c.h"
#if MCU==T_4_1
  #include "teensy.h"
#endif

// control pins (ADC_EN, ADC_SHDNZ, and HP_ON or ADC_EN2 where fitted) are defined in global.h

// one codec per TDM data line: ADDR pins L,L (0x4C) and H,L (0x4D)
static const uint8_t i2c_addr[2] = {0x4C, 0x4D};

// active input channels (bits 7..4 = ch1..ch4) and TDM slot map
#if NPORT==2      // Teensy 4.1: ch1, ch2 of each codec in slots 0, 1; with the microPAM_V5
                  // wiring (0x4C ch1/ch2 = channels 0/2, 0x4D ch1/ch2 = channels 1/3) the
                  // port-interleaved frame reads out as channels 0..3
  static const uint8_t chanMask = 0b1100<<4;
  static const uint8_t chmap[4] = {0,1,2,3};
#elif NCH==1
  #if PREAMP==2   // Mark's preamp on ch1
    static const uint8_t chanMask = 0b1000<<4;
    static const uint8_t chmap[4] = {0,1,2,3};
  #else           // preamp on ch2
    static const uint8_t chanMask = 0b0100<<4;
    static const uint8_t chmap[4] = {3,0,1,2};
  #endif
#elif NCH==2
  static const uint8_t chanMask = 0b0110<<4;
  static const uint8_t chmap[4] = {3,0,1,2};
#elif NCH==4      // ch1..ch4 in slots 0..3
  static const uint8_t chanMask = 0b1111<<4;
  static const uint8_t chmap[4] = {0,1,2,3};
#endif

static const uint8_t regs[4] = {0x3C, 0x41, 0x46, 0x4B};  // CHx_CFG0

uint32_t again = AGAIN;                 // analog gain 0..42 dB
static const int32_t dgain = DGAIN;     // digital gain (-200..54)/2 dB

static void acqPower(int flag)
{ digitalWrite(ADC_EN, flag);
  delay(100);
}

static void adc_config(i2c_class &i2c, uint8_t I2C_ADDRESS);

void adc_init(void)
{
  #if MCU==T_4_1
    #if ADC_PWR
      usbPowerSetup();          // ADC board supply from the USB host port
    #else
      Serial.println("ADC_PWR 0: ADC board not powered (test build)");
    #endif
  #endif
  #if defined(ADC_EN2)
    pinMode(ADC_EN2, OUTPUT);   // digital-side LDO (cVAS3)
    digitalWrite(ADC_EN2, HIGH);
  #endif
  pinMode(ADC_EN, OUTPUT);
  acqPower(HIGH);

  #if defined(HP_ON)
    pinMode(HP_ON, OUTPUT);
    digitalWrite(HP_ON, LOW);
  #endif

  // reset ADC
  pinMode(ADC_SHDNZ, OUTPUT);
  digitalWrite(ADC_SHDNZ, LOW);
  delay(100);
  digitalWrite(ADC_SHDNZ, HIGH);

  i2c_class i2c(&Wire);
  for(int ii = 0; ii < NPORT; ii++)
  { if(!i2c.exist(i2c_addr[ii]))
    { Serial.printf("ADC I2C %x not found\n", i2c_addr[ii]);
      continue;
    }
    Serial.printf("found ADC %x\n", i2c_addr[ii]);
    adc_config(i2c, i2c_addr[ii]);
  }
}

static void adc_config(i2c_class &i2c, uint8_t I2C_ADDRESS)
{
  i2c.write(I2C_ADDRESS, 0x02, 0x81);         // 1.8V AREG, not sleep
  i2c.write(I2C_ADDRESS, 0x07, (3<<4));       // TDM, 32 bit, transmit on rising edge, zero fill
  i2c.write(I2C_ADDRESS, 0x08, 0x00);         // TX offset 0
  for(int jj = 0; jj < 4; jj++)
    i2c.write(I2C_ADDRESS, 0x0B+jj, chmap[jj]);  // ASI_CHx slot assignment

  i2c.write(I2C_ADDRESS, 0x73, chanMask);     // enable input channels
  i2c.write(I2C_ADDRESS, 0x74, chanMask);     // enable ASI output slots
  i2c.write(I2C_ADDRESS, 0x75, 0xE0);         // power up MIC bias, ADC, PLL

  i2c.write(I2C_ADDRESS, 0x3B, (6<<4)         // MIC bias = AVDD
                              |(0<<0));       // full scale VREF 2.75 V
  i2c.write(I2C_ADDRESS, 0x6B, (2<<4)         // ultra-low-latency decimation filter
                              |(1<<0));       // HP filter 0.00025*fs

  for(int jj = 0; jj < 4; jj++)
  { i2c.write(I2C_ADDRESS, regs[jj]+0, 0x88);       // line in, 20 kOhm
    i2c.write(I2C_ADDRESS, regs[jj]+1, again<<2);   // analog gain (dB in bits 7..2)
    i2c.write(I2C_ADDRESS, regs[jj]+2, 201+dgain);  // digital gain
    i2c.write(I2C_ADDRESS, regs[jj]+3, 0x80);       // no gain calibration
    i2c.write(I2C_ADDRESS, regs[jj]+4, 0x00);       // no phase calibration
  }
  delay(10); // settle before reading back (0x76 sometimes reads 0x00)
  Serial.print("0x15: "); Serial.println(i2c.read(I2C_ADDRESS, 0x15), HEX);
  Serial.print("0x73: "); Serial.println(i2c.read(I2C_ADDRESS, 0x73), HEX);
  Serial.print("0x74: "); Serial.println(i2c.read(I2C_ADDRESS, 0x74), HEX);
  Serial.print("0x76: "); Serial.println(i2c.read(I2C_ADDRESS, 0x76), HEX);
}

void adc_exit(void)
{ digitalWrite(ADC_SHDNZ, LOW);
  acqPower(LOW);
  #if defined(ADC_EN2)
    digitalWrite(ADC_EN2, LOW);
  #endif
  #if MCU==T_4_1
    usbPowerOff();
    usbPowerExit();
  #endif
}

void setAGain(uint32_t again_dB)
{ i2c_class i2c(&Wire);
  for(int ii = 0; ii < NPORT; ii++)
    for(int jj = 0; jj < 4; jj++)
      i2c.write(i2c_addr[ii], regs[jj]+1, again_dB<<2);
}
