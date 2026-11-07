/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
// Teensy 4.1 platform extras. The platform API shared with the RP targets (acquisition,
// hibernation, UID, NeoPixel) is declared in rp2x.h and implemented in teensy.cxx.
#ifndef TEENSY_H
#define TEENSY_H
#include <stdint.h>

  void lowPowerInit(void);      // WFI enters wait mode, memory kept powered
  void phyPowerDown(void);      // Ethernet PHY to power-down (PHY_OFF in config.h)
  void powerSteps(uint32_t mask); // 1: unused PLLs off; 2: core from crystal; 4: clock gates
  void clockReport(void);       // PLL, divider and clock-gate registers
  void printCrashReport(void);  // report of a previous crash, if any
  uint32_t getPSRAMSize(void);  // bytes of external PSRAM (0 if none fitted)
  int alarmWake(void);          // 1 if the SNVS alarm has fired (power-on by alarm)

  // 5 V from the USB host port (powers the ADC board)
  void usbPowerSetup(void);
  void usbPowerOff(void);
  void usbPowerExit(void);

#endif
