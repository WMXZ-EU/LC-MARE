/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
// Single-producer / single-consumer queue of NBLOCK-word blocks.
//
// The producer (DMA IRQ on core 1) appends acquisition chunks to the block at the head;
// the consumer (loop() on core 0) removes whole blocks from the tail.
// head and tail are free-running block counters, written only by producer and consumer
// respectively, so no lock is needed. They wrap after 2^32 blocks (centuries of recording).
//
// Block layout
//   PROC_MODE 0: MD raw chunks of NBUF_ACQ words, the block is full.
//   PROC_MODE 1: variable-length compressed chunks, zero padded; the last word of the
//                block holds the number of valid words.
//
#include <string.h>
#include "Arduino.h"
#include "global.h"
#include "queue.h"

#if MCU==T_4_1
  // producer is the DMA ISR, consumer loop(), both on the one core
  #define __dmb() asm volatile("dmb" ::: "memory")
#else
  #include "hardware/sync.h"
#endif

#if MCU==T_4_1
  // queue in PSRAM (EXTMEM) if enough is fitted, else in RAM2 (DMAMEM); chosen by queueInit()
  #include "teensy.h"
  EXTMEM static uint32_t psram_buffer[MAX_QUEUE_PSRAM][NBLOCK];
  DMAMEM static uint32_t dmamem_buffer[MAX_QUEUE][NBLOCK];
  static uint32_t (*data_buffer)[NBLOCK] = dmamem_buffer;
  static uint32_t nqueue = MAX_QUEUE;

  // before acquisition starts: an access to EXTMEM without PSRAM would hard-fault
  void queueInit(void)
  { if(getPSRAMSize() >= sizeof(psram_buffer))
    { data_buffer = psram_buffer;
      nqueue = MAX_QUEUE_PSRAM;
    }
    Serial.printf("queue: %lu blocks of %lu kB in %s\n", nqueue, (uint32_t)(4*NBLOCK/1024),
                  (data_buffer == psram_buffer) ? "PSRAM" : "DMAMEM (PSRAM missing or too small)");
  }
#else
  #if defined(RP2350_PSRAM_CS)
    static uint32_t data_buffer[MAX_QUEUE][NBLOCK] PSRAM;
  #else
    static uint32_t data_buffer[MAX_QUEUE][NBLOCK];
  #endif
  static const uint32_t nqueue = MAX_QUEUE;
  void queueInit(void) {}
#endif

uint32_t queueBytes(void) { return nqueue*NBLOCK*4; }

static volatile uint32_t head = 0;  // blocks committed by producer
static volatile uint32_t tail = 0;  // blocks released by consumer
static uint32_t nfill = 0;          // words in the block being filled (producer only)

#if PROC_MODE==1
  #define NMAX (NBLOCK-1)           // last word is reserved for the fill count
#else
  #define NMAX (NBLOCK)
#endif

uint16_t __not_in_flash_func(getQueueCount)(void)
{ return (uint16_t)(head - tail);
}

static inline void __not_in_flash_func(commit)(void)
{ __dmb();                          // block content visible before head moves
  head = head + 1;
  nfill = 0;
}

// append ndat words; returns 0 if the queue is full and the chunk was dropped
uint16_t __not_in_flash_func(pushQueue)(const uint32_t *data, uint32_t ndat)
{
  if(nfill + ndat > NMAX)
  { // close current block (compressed mode only; raw blocks are closed when full)
    uint32_t *blk = data_buffer[head % nqueue];
    for(uint32_t ii = nfill; ii < NBLOCK; ii++) blk[ii] = 0;
    blk[NBLOCK-1] = nfill;
    commit();
  }
  // a partially filled block is never dropped: once started, its slot is owned by the producer
  if(nfill == 0 && (head - tail) >= nqueue) return 0;

  uint32_t *blk = data_buffer[head % nqueue];
  memcpy(&blk[nfill], data, 4*ndat);
  nfill += ndat;

  #if PROC_MODE==0
    if(nfill == NBLOCK) commit();
  #endif
  return 1;
}

// copy oldest block to dst; returns 0 if queue is empty
uint16_t pullQueue(uint32_t *dst)
{
  if(head == tail) return 0;
  __dmb();
  memcpy(dst, data_buffer[tail % nqueue], 4*NBLOCK);
  __dmb();                          // finished reading before slot is released
  tail = tail + 1;
  return 1;
}
