/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
// Teensy 4.1 platform: TDM acquisition (SAI1 + DMA), power-off hibernation, UID.
// Implements the platform API of rp2x.h; the acquisition code follows microPAM_V5 (Teensy.cxx).
#include "global.h"
#if MCU==T_4_1

#include "Arduino.h"
#include <CrashReport.h>
#include <DMAChannel.h>

#include "rp2x.h"
#include "teensy.h"
#include "rtc.h"
#include "queue.h"
#include "compress.h"
#include "filing.h"
#include "adc.h"

/************************************** misc ********************************************/
extern "C" uint8_t external_psram_size;   // MB, set by the Teensy startup code
uint32_t getPSRAMSize(void) { return (uint32_t)external_psram_size * 1024u * 1024u; }

void printCrashReport(void)
{ if(CrashReport) Serial.print(CrashReport);
}

// Ethernet PHY (DP83825I, Teensy 4.1 only): powered from 3.3 V whether used or not.
// GPIO_B0_15 (GPIO7 bit 15) is its power-down input, GPIO_B0_14 (bit 14) its reset, both
// active low (pin use as in FNET's fnet_mimxrt_eth.c); both are driven low.
void phyPowerDown(void)
{
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_14 = 5;     // ALT5: GPIO
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_15 = 5;
  GPIO7_GDIR |= (1<<14) | (1<<15);
  GPIO7_DR_CLEAR = (1<<14) | (1<<15);       // reset and power down
}

/************************************** power reduction *******************************/
// Steps selected by T41_POWER (config.h) at boot, or one at a time from the menu ('z').
//   1  PLLs nothing uses: video (PLL5), Ethernet (PLL6), USB2 (PLL7)
//   2  core from the 24 MHz crystal instead of the ARM PLL (768 MHz / 32), ARM PLL off;
//      only at 24 MHz, where the core already runs at the crystal frequency
//   4  clock gates of unused peripherals closed (CAN, LPUART, LPSPI, LPI2C2-4, FlexIO,
//      PWM, QTIMER, ENC, GPT, ACMP, ADC2, ENET, SEMC, CSI, LCD, PXP, SAI2/3, SPDIF, ...)
// The PLLs and gates in use stay: PLL2 (SDIO), PLL3 (USB, FlexSPI), PLL4 (SAI1), LPI2C1
// (Wire), ADC1 (battery), USDHC1, SAI1, DMA, USB, FlexSPI, FlexSPI2 (PSRAM), GPIO, PIT, SNVS, OCOTP.

static void pllOff(void)
{ CCM_ANALOG_PLL_VIDEO = CCM_ANALOG_PLL_VIDEO_POWERDOWN | CCM_ANALOG_PLL_VIDEO_BYPASS;
  CCM_ANALOG_PLL_ENET  = CCM_ANALOG_PLL_ENET_POWERDOWN  | CCM_ANALOG_PLL_ENET_BYPASS;
  CCM_ANALOG_PLL_USB2  = CCM_ANALOG_PLL_USB2_BYPASS;    // POWER, ENABLE, EN_USB_CLKS cleared
}

static void coreFromOsc(void)
{ if(F_CPU_ACTUAL != 24000000) { Serial.println("core from OSC needs speed=24"); return; }
  __disable_irq();
  // periph_clk2 = 24 MHz crystal, undivided
  CCM_CBCMR = (CCM_CBCMR & ~CCM_CBCMR_PERIPH_CLK2_SEL_MASK) | CCM_CBCMR_PERIPH_CLK2_SEL(1);
  while (CCM_CDHIPR & CCM_CDHIPR_PERIPH2_CLK_SEL_BUSY);
  CCM_CBCDR &= ~CCM_CBCDR_PERIPH_CLK2_PODF_MASK;
  // periph_clk from periph_clk2 (AHB briefly 6 MHz with the present divider 4), then AHB /1
  CCM_CBCDR |= CCM_CBCDR_PERIPH_CLK_SEL;
  while (CCM_CDHIPR & CCM_CDHIPR_PERIPH_CLK_SEL_BUSY);
  CCM_CBCDR &= ~(CCM_CBCDR_AHB_PODF_MASK | CCM_CBCDR_IPG_PODF_MASK);  // AHB = IPG = 24 MHz
  while (CCM_CDHIPR & CCM_CDHIPR_AHB_PODF_BUSY);
  CCM_ANALOG_PLL_ARM = CCM_ANALOG_PLL_ARM_POWERDOWN | CCM_ANALOG_PLL_ARM_BYPASS;
  __enable_irq();
}

static void clockGatesOff(void)
{ const uint32_t off = CCM_CCGR_ON;   // 3 in a gate field
  CCM_CCGR0 &= ~(CCM_CCGR0_LPUART2(off) | CCM_CCGR0_GPT2_SERIAL(off) | CCM_CCGR0_GPT2_BUS(off)
               | CCM_CCGR0_TRACE(off) | CCM_CCGR0_CAN2_SERIAL(off) | CCM_CCGR0_CAN2(off)
               | CCM_CCGR0_CAN1_SERIAL(off) | CCM_CCGR0_CAN1(off) | CCM_CCGR0_LPUART3(off)
               | CCM_CCGR0_DCP(off) | CCM_CCGR0_MQS_HMCLK(off));
  CCM_CCGR1 &= ~(CCM_CCGR1_LPUART4(off) | CCM_CCGR1_GPT1_SERIAL(off) | CCM_CCGR1_GPT1_BUS(off)
               | CCM_CCGR1_AOI2(off) | CCM_CCGR1_ENET(off) | CCM_CCGR1_ADC2(off)
               | CCM_CCGR1_LPSPI1(off) | CCM_CCGR1_LPSPI2(off) | CCM_CCGR1_LPSPI3(off)
               | CCM_CCGR1_LPSPI4(off));
  CCM_CCGR2 &= ~(CCM_CCGR2_PXP(off) | CCM_CCGR2_LCD(off) | CCM_CCGR2_XBAR2(off)
               | CCM_CCGR2_XBAR3(off) | CCM_CCGR2_LPI2C2(off) | CCM_CCGR2_LPI2C3(off)
               | CCM_CCGR2_CSI(off));
  CCM_CCGR3 &= ~(CCM_CCGR3_ACMP1(off) | CCM_CCGR3_ACMP2(off) | CCM_CCGR3_ACMP3(off)
               | CCM_CCGR3_ACMP4(off) | CCM_CCGR3_EWM(off) | CCM_CCGR3_LCDIF_PIX(off)
               | CCM_CCGR3_AOI1(off) | CCM_CCGR3_LPUART5(off) | CCM_CCGR3_LPUART6(off)
               | CCM_CCGR3_SEMC(off) | CCM_CCGR3_FLEXIO2(off));
  CCM_CCGR4 &= ~(CCM_CCGR4_ENC1(off) | CCM_CCGR4_ENC2(off) | CCM_CCGR4_ENC3(off)
               | CCM_CCGR4_ENC4(off) | CCM_CCGR4_PWM1(off) | CCM_CCGR4_PWM2(off)
               | CCM_CCGR4_PWM3(off) | CCM_CCGR4_PWM4(off) | CCM_CCGR4_TSC(off));
  CCM_CCGR5 &= ~(CCM_CCGR5_LPUART7(off) | CCM_CCGR5_LPUART1(off) | CCM_CCGR5_SAI2(off)
               | CCM_CCGR5_SAI3(off) | CCM_CCGR5_SPDIF(off) | CCM_CCGR5_KPP(off)
               | CCM_CCGR5_FLEXIO1(off));
  CCM_CCGR6 &= ~(CCM_CCGR6_QTIMER1(off) | CCM_CCGR6_QTIMER2(off) | CCM_CCGR6_QTIMER3(off)
               | CCM_CCGR6_QTIMER4(off) | CCM_CCGR6_LPI2C4_SERIAL(off) | CCM_CCGR6_LPUART8(off)
               | CCM_CCGR6_TRNG(off) | CCM_CCGR6_USDHC2(off));
  // FLEXSPI2 stays on: it is the PSRAM interface, which holds the queue
  CCM_CCGR7 &= ~(CCM_CCGR7_FLEXIO3(off) | CCM_CCGR7_CAN3_SERIAL(off) | CCM_CCGR7_CAN3(off)
               | CCM_CCGR7_ENET2(off));
}

void clockReport(void)
{ Serial.printf("F_CPU %lu, F_BUS %lu, DCDC %lu mV\n", F_CPU_ACTUAL, F_BUS_ACTUAL,
                800 + 25*((DCDC_REG3 & DCDC_REG3_TRG_MASK) >> 0));
  Serial.printf("PLL ARM %08lX SYS %08lX USB1 %08lX AUDIO %08lX VIDEO %08lX ENET %08lX USB2 %08lX\n",
                CCM_ANALOG_PLL_ARM, CCM_ANALOG_PLL_SYS, CCM_ANALOG_PLL_USB1, CCM_ANALOG_PLL_AUDIO,
                CCM_ANALOG_PLL_VIDEO, CCM_ANALOG_PLL_ENET, CCM_ANALOG_PLL_USB2);
  Serial.printf("PFD 528 %08lX 480 %08lX CBCDR %08lX CBCMR %08lX CSCMR1 %08lX\n",
                CCM_ANALOG_PFD_528, CCM_ANALOG_PFD_480, CCM_CBCDR, CCM_CBCMR, CCM_CSCMR1);
  Serial.printf("CCGR0..7 %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX\n",
                CCM_CCGR0, CCM_CCGR1, CCM_CCGR2, CCM_CCGR3, CCM_CCGR4, CCM_CCGR5, CCM_CCGR6, CCM_CCGR7);
}

// apply the steps in mask (1, 2, 4); 0 prints the clock report only
void powerSteps(uint32_t mask)
{ if(mask & 1) pllOff();
  if(mask & 2) coreFromOsc();
  if(mask & 4) clockGatesOff();
  clockReport();
}

void lowPowerInit(void)
{ CCM_CGPR |= CCM_CGPR_INT_MEM_CLK_LPM;      // keep memory powered in low-power mode
  CCM_CLPCR &= ~(CCM_CLPCR_ARM_CLK_DIS_ON_LPM | CCM_CLPCR_LPM(3));  // CPU clock on (SysTick wakes)
  CCM_CLPCR |= CCM_CLPCR_LPM(1);            // WFI enters wait mode
  asm volatile("dsb");
}

// USB host 5 V (GPIO_EMC_40, has a 100 uF capacitor)
static void usbPowerInit(void)
{ IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_40 = 5;
  IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_40 = 0x0008;  // slow speed, weak 150 Ohm drive
  GPIO8_GDIR |= 1<<26;
}
static void usbPowerOn(void) { GPIO8_DR_SET = 1<<26; }
void usbPowerOff(void)       { GPIO8_DR_CLEAR = 1<<26; }
void usbPowerExit(void)
{ IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_40 = 0;
  GPIO8_GDIR &= ~(1<<26);
}
void usbPowerSetup(void)
{ usbPowerInit();
  usbPowerOn();
  delay(100);
}

/************************************** TDM input (SAI1) ********************************/
// SAI1 is bus master: MCLK pin 23, BCLK pin 21, FSYNC pin 20, data pin 8 (codec 0x4C) and,
// with NPORT 2, pin 6 (codec 0x4D). A frame has NSLOT 32-bit slots on each line, of which
// NCH/NPORT carry data; the remaining slots are masked (RMR), so that the FIFO receives only
// active slots (2 channels: slots 0, 1 = ADC ch2, ch3).
#define NSLOT   4                     // slots per frame and line (bit clock 128*fs)
#define NUSED   (NCH/NPORT)           // active slots per line
#define MSYNC   1                     // frame sync width (bit clocks)
#define MDIV    1                     // BCLK = MCLK/(2*MDIV)

uint32_t fsamp = FSAMP;
static uint16_t acq_isrunning = 0;

// PLL4 (audio PLL) = 24 MHz * (nfact + nmult/ndiv)
static void set_audioClock(int nfact, int32_t nmult, uint32_t ndiv)
{
  CCM_ANALOG_PLL_AUDIO = CCM_ANALOG_PLL_AUDIO_BYPASS | CCM_ANALOG_PLL_AUDIO_ENABLE
                       | CCM_ANALOG_PLL_AUDIO_POST_DIV_SELECT(2)    // 2: 1/1; 1: 1/2; 0: 1/4
                       | CCM_ANALOG_PLL_AUDIO_DIV_SELECT(nfact);
  CCM_ANALOG_PLL_AUDIO_NUM   = nmult & CCM_ANALOG_PLL_AUDIO_NUM_MASK;
  CCM_ANALOG_PLL_AUDIO_DENOM = ndiv & CCM_ANALOG_PLL_AUDIO_DENOM_MASK;

  CCM_ANALOG_PLL_AUDIO &= ~CCM_ANALOG_PLL_AUDIO_POWERDOWN;          // switch on PLL
  while (!(CCM_ANALOG_PLL_AUDIO & CCM_ANALOG_PLL_AUDIO_LOCK)) {};   // wait for lock

  CCM_ANALOG_MISC2 &= ~(CCM_ANALOG_MISC2_DIV_MSB | CCM_ANALOG_MISC2_DIV_LSB);  // no post divider
  CCM_ANALOG_PLL_AUDIO &= ~CCM_ANALOG_PLL_AUDIO_BYPASS;
}

// MCLK = 256*fs (ADC oversampling 128, doubled for TDM)
static void setAudioFrequency(uint32_t fs)
{
  const int ovr = 128;
  int n1 = 4;                                       // SAI prescaler (n1*n2 multiple of 4)
  int n2 = 1 + (24000000 * 27) / (fs * ovr * n1);
  double C = ((double)fs * ovr * n1 * n2) / 24000000;
  int c0 = C;
  int c2 = 10000;
  int c1 = C * c2 - (c0 * c2);
  set_audioClock(c0, c1, c2);

  n1 = n1 / 2;                                      // double speed for TDM
  CCM_CSCMR1 = (CCM_CSCMR1 & ~(CCM_CSCMR1_SAI1_CLK_SEL_MASK))
             | CCM_CSCMR1_SAI1_CLK_SEL(2);          // 0: PLL3PFD0; 1: PLL5; 2: PLL4
  CCM_CS1CDR = (CCM_CS1CDR & ~(CCM_CS1CDR_SAI1_CLK_PRED_MASK | CCM_CS1CDR_SAI1_CLK_PODF_MASK))
             | CCM_CS1CDR_SAI1_CLK_PRED(n1-1)
             | CCM_CS1CDR_SAI1_CLK_PODF(n2-1);
  IOMUXC_GPR_GPR1 = (IOMUXC_GPR_GPR1 & ~(IOMUXC_GPR_GPR1_SAI1_MCLK1_SEL_MASK))
                  | (IOMUXC_GPR_GPR1_SAI1_MCLK_DIR | IOMUXC_GPR_GPR1_SAI1_MCLK1_SEL(0));
}

void acqModifyFrequency(uint32_t fs)
{
  fsamp = fs;
  if(!acq_isrunning) return;   // not yet set up: i2s_setup() applies fsamp (SAI clock is gated)
  I2S1_RCSR &= ~(I2S_RCSR_RE | I2S_RCSR_BCE);
  setAudioFrequency(fsamp);
  I2S1_RCSR |= I2S_RCSR_RE | I2S_RCSR_BCE;
}

void i2s_setup(void)
{
  CCM_CCGR5 |= CCM_CCGR5_SAI1(CCM_CCGR_ON);
  if (I2S1_RCSR & I2S_RCSR_RE) return;   // receiver already running

  setAudioFrequency(fsamp);
  acq_isrunning = 1;

  CORE_PIN23_CONFIG = 3;    // MCLK
  CORE_PIN21_CONFIG = 3;    // RX_BCLK
  CORE_PIN20_CONFIG = 3;    // RX_SYNC
  CORE_PIN8_CONFIG  = 3;    // RX_DATA0
  IOMUXC_SAI1_RX_DATA0_SELECT_INPUT = 2;
  #if NPORT==2
    CORE_PIN6_CONFIG = 3;   // RX_DATA1
    IOMUXC_SAI1_RX_DATA1_SELECT_INPUT = 1;
  #endif

  I2S1_RMR  = ((1u << NSLOT) - 1) & ~((1u << NUSED) - 1);   // mask unused slots
  I2S1_RCR1 = I2S_RCR1_RFW(4);
  I2S1_RCR2 = I2S_RCR2_SYNC(0) | I2S_RCR2_BCD | I2S_RCR2_DIV((MDIV-1)) | I2S_RCR2_MSEL(1);
  #if NPORT==1
    I2S1_RCR3 = I2S_RCR3_RCE;
  #else
    I2S1_RCR3 = I2S_RCR3_RCE_2CH;
  #endif
  I2S1_RCR4 = I2S_RCR4_FRSZ((NSLOT-1)) | I2S_RCR4_SYWD((MSYNC-1)) | I2S_RCR4_MF
            | I2S_RCR4_FSE | I2S_RCR4_FSP | I2S_RCR4_FSD;
  I2S1_RCR5 = I2S_RCR5_WNW((MBIT-1)) | I2S_RCR5_W0W((MBIT-1)) | I2S_RCR5_FBT((MBIT-1));

  I2S1_RCSR = I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR;
}

/************************************** DMA *********************************************/
// One circular buffer of two halves, interrupt at half and at end (ping-pong).
// With two lines the minor loop reads RDR0 then RDR1, so a frame arrives port-interleaved
// (0x4C slot 0, 0x4D slot 0, 0x4C slot 1, 0x4D slot 1); the ADC board wiring maps this to
// channels 0..3 (microPAM_V5 convention).
static DMAChannel dma;
DMAMEM __attribute__((aligned(32))) static uint32_t i2s_buffer[2*NBUF_ACQ];
static int32_t acq_buffer[NBUF_ACQ];   // work buffer (compressed in place)

volatile uint32_t missed_acq = 0;
volatile uint32_t acq_count = 0;

static void acq_isr(void)
{
  uint32_t daddr = (uint32_t)(dma.TCD->DADDR);
  dma.clearInterrupt();

  // DMA writes to one half: take the other
  const uint32_t *src = (daddr < (uint32_t)&i2s_buffer[NBUF_ACQ]) ? &i2s_buffer[NBUF_ACQ] : &i2s_buffer[0];
  arm_dcache_delete((void *)src, sizeof(i2s_buffer)/2);
  memcpy(acq_buffer, src, 4*NBUF_ACQ);
  acq_count++;

  #if PROC_MODE==0
    uint32_t ndat = NBUF_ACQ;
  #else
    uint32_t ndat = compressData(acq_buffer);
  #endif
  if(!pushQueue((uint32_t *)acq_buffer, ndat)) missed_acq++;
}

void dma_setup(void)
{
  dma.begin(true);

  dma.TCD->SADDR = (void *)((uint32_t)&I2S1_RDR0);
  dma.TCD->DADDR = i2s_buffer;
  dma.TCD->DOFF  = (MBIT/8);
  dma.TCD->ATTR  = DMA_TCD_ATTR_SSIZE((MBIT/16)) | DMA_TCD_ATTR_DSIZE((MBIT/16));
  dma.TCD->DLASTSGA = -sizeof(i2s_buffer);
  #if NPORT==1
    dma.TCD->SOFF  = 0;
    dma.TCD->NBYTES_MLNO = (MBIT/8);
    dma.TCD->SLAST = 0;
    dma.TCD->CITER_ELINKNO = 2*NBUF_ACQ;
  #else
    // two-word minor loop: source steps RDR0 -> RDR1 and is rewound by 8 bytes
    dma.TCD->SOFF  = 4;
    dma.TCD->NBYTES_MLOFFYES = DMA_TCD_NBYTES_SMLOE
                             | DMA_TCD_NBYTES_MLOFFYES_MLOFF(-8)
                             | DMA_TCD_NBYTES_MLOFFYES_NBYTES(8);
    dma.TCD->SLAST = -8;
    dma.TCD->CITER_ELINKNO = NBUF_ACQ;
  #endif
  dma.TCD->BITER_ELINKNO = dma.TCD->CITER_ELINKNO;
  dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;

  dma.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI1_RX);
  dma.attachInterrupt(acq_isr, 0x50);   // above default priority (128)
  dma.enable();
}

/************************************** power *******************************************/
void doReboot(void) { SCB_AIRCR = 0x05FA0004; while(1); }

int alarmWake(void) { return (SNVS_LPSR & 1); }   // LPTA: time alarm flag

// switch off the 3.3 V regulator (SNVS TOP); the SNVS alarm switches it on again
static void powerOff(void)
{ SNVS_LPCR |= (1 << 6);
  while(1) asm("wfi");
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

// power off until secs (rtc_get() scale, seconds since 1970); the program restarts on wake-up
void hibernate_until(uint32_t secs)
{
  if(secs <= rtc_get() + 1)
  { // the SNVS alarm fires on a match only: an alarm in the past would never wake the unit
    Serial.println("alarm not in the future: reboot");
    SD_stop();
    delay(200);
    doReboot();
  }
  XRTCsetAlarm(secs);
  // wake-up time is stored as start time in config.txt (by SD_stop)
  datetime_t tm;
  time2date(secs, &tm, RTC_EPOCH);
  encodeTimestamp(startTime, &tm);
  SD_stop();
  adc_exit();
  delay(200);
  powerOff();
}

/************************************** UID *********************************************/
char uid_strng[10];
void getUID(void)
{ sprintf(uid_strng, "%08lX", (HW_OCOTP_MAC0 & 0xFFFFFFFF));
}

/************************************** NeoPixel (none) *********************************/
void neo_pixel_init(void) {}
void neo_pixel_show(uint16_t r, uint16_t g, uint16_t b) { (void)r; (void)g; (void)b; }

#endif
