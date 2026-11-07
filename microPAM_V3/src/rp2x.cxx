/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
#include "global.h"
#if MCU==RP_2040 || MCU==RP_2350

#include "Arduino.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/xosc.h"
#include "hardware/structs/rosc.h"
#include "pico/unique_id.h"
#include "pico/runtime_init.h"

#if MCU==RP_2350
  #include "hardware/powman.h"
#endif
#include "rp2x.h"
#include "rtc.h"
#include "queue.h"
#include "compress.h"
#include "filing.h"

/************************************** TDM input (PIO) *********************************/
#define I2S_DOUT  11
#define I2S_BCLK  9   // FSYNC is always BCLK+1 (side-set pins)

// PIO instruction encoders (side-set: bit0 = BCLK, bit1 = FSYNC)
#define SETX(SIDE,VAL)  ((7<<13)| (SIDE<<11)| (1<<5)| (VAL))
#define SETY(SIDE,VAL)  ((7<<13)| (SIDE<<11)| (2<<5)| (VAL))
#define JMPX(SIDE,VAL)  ((0<<13)| (SIDE<<11)| (2<<5)| (VAL))
#define JMPY(SIDE,VAL)  ((0<<13)| (SIDE<<11)| (4<<5)| (VAL))
#define NOP(SIDE)       ((5<<13)| (SIDE<<11)| (2<<5)| (2))
#define INP(SIDE,VAL)   ((2<<13)| (SIDE<<11)| (0<<5)| (VAL))  // in pins
#define INN(SIDE,VAL)   ((2<<13)| (SIDE<<11)| (3<<5)| (VAL))  // in null

#if NCH==1
  // one 32-bit slot per frame
  #define pio_tdm_in_wrap_target 0
  #define pio_tdm_in_wrap 5
  static const uint16_t pio_tdm_in_program_instructions[] = {
    SETX(0b11,MBIT-3),
     INP(0b10,1),
     NOP(0b11),
     INP(0b00,1),
    JMPX(0b01,3),
     INN(0b10,1)
  };
#else
  // NCH 32-bit slots per frame (NCH >= 2)
  #define pio_tdm_in_wrap_target 0
  #define pio_tdm_in_wrap 11
  static const uint16_t pio_tdm_in_program_instructions[] = {
    SETY(0b11,NCH-2),
     INP(0b10,1),
    SETX(0b11,MBIT-3),
     INP(0b00,1),
    JMPX(0b01,3),
     INP(0b00,1),
    SETX(0b01,MBIT-3),
     INP(0b00,1),
    JMPX(0b01,7),
     INP(0b00,1),
    JMPY(0b01,5),
     INP(0b10,1)
  };
#endif

static const struct pio_program pio_tdm_in_program = {
  .instructions = pio_tdm_in_program_instructions,
  .length = sizeof(pio_tdm_in_program_instructions)/sizeof(uint16_t),
  .origin = -1,
};

static PIO _pio = nullptr;
static int _sm;
static int _off;

uint32_t fsamp = FSAMP;

static void pio_tdm_in_program_init(PIO pio, uint sm, uint offset, uint data_pin, uint clock_pin_base, uint bits)
{
  pio_gpio_init(pio, data_pin);
  pio_gpio_init(pio, clock_pin_base);
  pio_gpio_init(pio, clock_pin_base + 1);

  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_wrap(&c, offset + pio_tdm_in_wrap_target, offset + pio_tdm_in_wrap);
  sm_config_set_sideset(&c, 2, false, false);
  sm_config_set_in_pins(&c, data_pin);
  sm_config_set_sideset_pins(&c, clock_pin_base);
  sm_config_set_in_shift(&c, false, true, bits);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
  pio_sm_init(pio, sm, offset, &c);

  uint pin_mask = 3u << clock_pin_base;
  pio_sm_set_pindirs_with_mask(pio, sm, pin_mask, pin_mask);
  pio_sm_set_pins(pio, sm, 0);
}

void acqModifyFrequency(uint32_t fs)
{
  fsamp = fs;
  if(!_pio) return;   // not yet set up: i2s_setup() applies fsamp
  float bitClk = fsamp * MBIT * NCH * 2.0f;  // two PIO instructions per bit
  pio_sm_set_enabled(_pio, _sm, false);
  pio_sm_set_clkdiv(_pio, _sm, (float)clock_get_hz(clk_sys) / bitClk);
  pio_sm_set_enabled(_pio, _sm, true);
}

void i2s_setup(void)
{
  static PIOProgram tdm_program(&pio_tdm_in_program);
  PIO pio;
  tdm_program.prepare(&pio, &_sm, &_off);
  pio_tdm_in_program_init(pio, _sm, _off, I2S_DOUT, I2S_BCLK, MBIT);
  _pio = pio;
  acqModifyFrequency(fsamp);  // also starts the state machine
}

/************************************** DMA *********************************************/
static int _channelDMA[2];
static int32_t i2s_buffer[3][NBUF_ACQ];   // two DMA buffers + one work buffer

volatile uint32_t missed_acq = 0;
volatile uint32_t acq_count = 0;

static void __not_in_flash_func(dma_irq)(void)
{
  for(int ii = 0; ii < 2; ii++)
  { if(!dma_channel_get_irq0_status(_channelDMA[ii])) continue;

    dma_channel_acknowledge_irq0(_channelDMA[ii]);
    memcpy(i2s_buffer[2], i2s_buffer[ii], 4*NBUF_ACQ);
    dma_channel_set_write_addr(_channelDMA[ii], i2s_buffer[ii], false);
    dma_channel_set_trans_count(_channelDMA[ii], NBUF_ACQ, false);
    acq_count++;

    #if PROC_MODE==0
      uint32_t ndat = NBUF_ACQ;
    #else
      uint32_t ndat = compressData(i2s_buffer[2]);
    #endif
    if(!pushQueue((uint32_t *)i2s_buffer[2], ndat)) missed_acq++;
    return;
  }
}

// ping-pong DMA from PIO RX FIFO; must run on the acquisition core (IRQ is per core)
void dma_setup(void)
{
  for (int ii = 0; ii < 2; ii++) _channelDMA[ii] = dma_claim_unused_channel(true);

  int dreq = pio_get_dreq(_pio, _sm, false);
  volatile void *pioFIFOAddr = (volatile void *)&_pio->rxf[_sm];

  for (int ii = 0; ii < 2; ii++)
  { dma_channel_config c = dma_channel_get_default_config(_channelDMA[ii]);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, dreq);
    channel_config_set_chain_to(&c, _channelDMA[ii ^ 1]);
    channel_config_set_irq_quiet(&c, false);
    dma_channel_configure(_channelDMA[ii], &c, i2s_buffer[ii], pioFIFOAddr, NBUF_ACQ, false);
    dma_channel_set_irq0_enabled(_channelDMA[ii], true);
  }

  irq_add_shared_handler(DMA_IRQ_0, dma_irq, 0x60);  // above default (0x80) order priority
  irq_set_enabled(DMA_IRQ_0, true);

  dma_channel_start(_channelDMA[0]);
}

/************************************** Dormant sleep ***********************************/
static inline void rosc_write(io_rw_32 *addr, uint32_t value)
{ hw_clear_bits(&rosc_hw->status, ROSC_STATUS_BADWRITE_BITS);
  *addr = value;
}

static void rosc_disable(void)
{ uint32_t tmp = rosc_hw->ctrl;
  tmp &= (~ROSC_CTRL_ENABLE_BITS);
  tmp |= (ROSC_CTRL_ENABLE_VALUE_DISABLE << ROSC_CTRL_ENABLE_LSB);
  rosc_write(&rosc_hw->ctrl, tmp);
  while(rosc_hw->status & ROSC_STATUS_STABLE_BITS);
}

static void rosc_enable(void)
{ rosc_write(&rosc_hw->ctrl, ROSC_CTRL_ENABLE_BITS);
  while (!(rosc_hw->status & ROSC_STATUS_STABLE_BITS));
}

// run everything from the 12 MHz crystal, stop PLLs and ring oscillator
static void sleep_run_from_xosc(void)
{
  uint src_hz = XOSC_HZ;
  clock_configure(clk_ref, CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC, 0, src_hz, src_hz);
  clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF, 0, src_hz, src_hz);
  clock_stop(clk_adc);
  clock_stop(clk_usb);
  #if MCU==RP_2350
    clock_stop(clk_hstx);
  #endif
  #if HAS_RP2040_RTC
    clock_stop(clk_rtc);
  #endif
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, src_hz, src_hz);
  pll_deinit(pll_sys);
  pll_deinit(pll_usb);
  rosc_disable();
}

static void sleep_goto_dormant_until_pin(uint32_t gpio_pin)
{
  uint32_t event = IO_BANK0_DORMANT_WAKE_INTE0_GPIO0_LEVEL_LOW_BITS;
  gpio_init(gpio_pin);
  gpio_set_input_enabled(gpio_pin, true);
  gpio_set_dormant_irq_enabled(gpio_pin, event, true);

  xosc_dormant();   // execution stops here until the pin goes low

  gpio_acknowledge_irq(gpio_pin, event);
  gpio_set_input_enabled(gpio_pin, false);
}

static void sleep_power_up(void)
{
  rosc_enable();
  clocks_hw->sleep_en0 |= ~(0u);
  clocks_hw->sleep_en1 |= ~(0u);
  clocks_init();
  #if MCU==RP_2350
    uint64_t restore_ms = powman_timer_get_ms();
    powman_timer_set_1khz_tick_source_xosc();
    powman_timer_set_ms(restore_ms);
  #endif
}

void doReboot(void) { rp2040.restart(); }

static void goDormant(void)
{
  for(int p = 0; p < (int)PINS_COUNT; p++)   // all pins to high-impedance, input gate off
  { pinMode(p, INPUT);
    gpio_set_input_enabled(p, false);
  }
  sleep_run_from_xosc();
  sleep_goto_dormant_until_pin(XRTC_INT_PIN);
  sleep_power_up();
  doReboot();                           // restart program on wake-up
}

static const char *t_fmt = "%4d-%02d-%02d_%02d:%02d:%02d";
void encodeTimestamp(char *txt, const datetime_t *tm)
{ sprintf(txt, t_fmt, tm->year, tm->month, tm->day, tm->hour, tm->min, tm->sec);
}
void decodeTimestamp(datetime_t *tm, const char *txt)
{ int vals[6] = {0};
  sscanf(txt, t_fmt, &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]);
  tm->year = vals[0]; tm->month = vals[1]; tm->day = vals[2];
  tm->hour = vals[3]; tm->min   = vals[4]; tm->sec = vals[5];
}

void hibernate_until(uint32_t secs)
{ XRTCsetAlarm(secs);
  // wake-up time is stored as start time in config.txt (by SD_stop)
  datetime_t tm;
  time2date(secs, &tm, 2000);
  encodeTimestamp(startTime, &tm);
  SD_stop();
  delay(200);
  goDormant();
}

/************************************** UID *********************************************/
char uid_strng[10];
void getUID(void)
{
  pico_unique_board_id_t id;
  pico_get_unique_board_id(&id);
  Serial.print("unique_board_id: ");
  for (int ii = 0; ii < 8; ii++) { Serial.print(id.id[ii], HEX); Serial.print(' '); }
  Serial.println();
  sprintf(uid_strng, "%02X%02X%02X%02X", id.id[4], id.id[5], id.id[6], id.id[7]);
}

/************************************** NeoPixel ****************************************/
#include <Adafruit_NeoPixel.h>
static Adafruit_NeoPixel neo_pixel(1, PIN_NEOPIXEL, NEO_GRB);

void neo_pixel_init(void)
{ neo_pixel.begin();
  neo_pixel.clear();
}

void neo_pixel_show(uint16_t r, uint16_t g, uint16_t b)
{ neo_pixel.setPixelColor(0, neo_pixel.Color(r, g, b));
  neo_pixel.show();
}

#endif
