/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
// Block queue between acquisition (core 1, DMA IRQ) and filing (core 0).
#ifndef QUEUE_H
#define QUEUE_H
#include <stdint.h>

  void queueInit(void);         // Teensy: queue in PSRAM if fitted (before acquisition starts)
  uint32_t queueBytes(void);    // queue size in bytes
  uint16_t getQueueCount(void);
  uint16_t pushQueue(const uint32_t *src, uint32_t ndat);
  uint16_t pullQueue(uint32_t *dst);

#endif
