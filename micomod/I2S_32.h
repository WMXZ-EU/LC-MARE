/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
 // this SW is modified from PJRC Audio library by WMXZ 
 // for use with the test_AudioI2S and derivatives
 // I2S fetches 32 bit data
 // lower 16 bit are selected for further processing (standard AudioStream)
 // a digital attenuation shifts data to lower 16 bit 
 //
 
#ifndef I2S_32_H
#define I2S_32_H

#include "core_pins.h"
#include "AudioStream.h"
#include "DMAChannel.h"

#include "i2s_mods.h"

class I2S_32 : public AudioStream
{
public:

	I2S_32(void) : AudioStream(0, NULL) {begin();}
  void begin(void);
  virtual void update(void);
  void digitalShift(int32_t val){I2S_32::shift=val;}
  
protected:  
  static bool update_responsibility;
  static DMAChannel dma;
  static void isr32(void);
  
private:
  static int32_t DC_left;
  static int32_t DC_right;
  static int32_t shift;
  static audio_block_t *block_left;
  static audio_block_t *block_right;
  static uint16_t block_offset;

  void config_i2s(void);
};

#define NAVG 16

// for 32 bit I2S we need doubled buffer
static uint32_t i2s_rx_buffer_32[2*AUDIO_BLOCK_SAMPLES];
int32_t I2S_32::shift=8; //8 shifts 24 bit data to LSB
int32_t I2S_32::DC_left=0;
int32_t I2S_32::DC_right=0;

audio_block_t * I2S_32:: block_left = NULL;
audio_block_t * I2S_32:: block_right = NULL;
uint16_t I2S_32:: block_offset = 0;
bool I2S_32::update_responsibility = false;
DMAChannel I2S_32::dma(false);

void I2S_32::begin(void)
{ 

  dma.begin(true); // Allocate the DMA channel first

  config_i2s();

#if defined(KINETISK)
  CORE_PIN13_CONFIG = PORT_PCR_MUX(4); // pin 13, PTC5, I2S0_RXD0
  dma.TCD->SADDR = (void *)((uint32_t)&I2S0_RDR0);

#elif defined (__IMXRT1062__)
	CORE_PIN8_CONFIG  = 3;  //1:RX_DATA0
	IOMUXC_SAI1_RX_DATA0_SELECT_INPUT = 2;
	dma.TCD->SADDR = (void *)((uint32_t)&I2S1_RDR0);
#endif

  dma.TCD->SOFF = 0;
  dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(2) | DMA_TCD_ATTR_DSIZE(2);
  dma.TCD->NBYTES_MLNO = 4;
  dma.TCD->SLAST = 0;
  dma.TCD->DADDR = i2s_rx_buffer_32;
  dma.TCD->DOFF = 4;
  dma.TCD->CITER_ELINKNO = sizeof(i2s_rx_buffer_32) / 4;
  dma.TCD->DLASTSGA = -sizeof(i2s_rx_buffer_32);
  dma.TCD->BITER_ELINKNO = sizeof(i2s_rx_buffer_32) / 4;
  dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;

#if defined(KINETISK)
  dma.triggerAtHardwareEvent(DMAMUX_SOURCE_I2S0_RX);
  I2S0_RCSR |= I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR;
  I2S0_TCSR |= I2S_TCSR_TE | I2S_TCSR_BCE; // TX clock enable, because sync'd to TX

#elif defined (__IMXRT1062__)
	dma.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI1_RX);
  I2S1_RCSR = I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR;
//  I2S1_TCSR |= I2S_TCSR_TE | I2S_TCSR_BCE; // TX clock enable, because sync'd to TX

#endif
  update_responsibility = update_setup();
  dma.enable();

  dma.attachInterrupt(isr32); 
}

void I2S_32::isr32(void)
{
  uint32_t daddr, offset;
  const int32_t *src, *end;

//  char * dest_left, *dest_right;
  
  int16_t *dest_left, *dest_right; 
  audio_block_t *left, *right;

  daddr = (uint32_t)(dma.TCD->DADDR);

  dma.clearInterrupt();
  
  if (daddr < (uint32_t)i2s_rx_buffer_32 + sizeof(i2s_rx_buffer_32) / 2) {
    // DMA is receiving to the first half of the buffer
    // need to remove data from the second half
    src = (int32_t *)&i2s_rx_buffer_32[AUDIO_BLOCK_SAMPLES];
    end = (int32_t *)&i2s_rx_buffer_32[AUDIO_BLOCK_SAMPLES*2];
    if (I2S_32::update_responsibility) AudioStream::update_all();
  } else {
    // DMA is receiving to the second half of the buffer
    // need to remove data from the first half
    src = (int32_t *)&i2s_rx_buffer_32[0];
    end = (int32_t *)&i2s_rx_buffer_32[AUDIO_BLOCK_SAMPLES];
  }

/*
  // remove DC from data
  int32_t *data = (int32_t *) src;
  int32_t dc0=0,dc1=0;
  for(int ii=0; ii<AUDIO_BLOCK_SAMPLES/2;ii++)
  {
    dc0 +=data[0+2*ii];
    dc1 +=data[1+2*ii];
  }
  dc0 /=AUDIO_BLOCK_SAMPLES/2;
  dc1 /=AUDIO_BLOCK_SAMPLES/2;

  for(int ii=0; ii<AUDIO_BLOCK_SAMPLES/2;ii++)
  {
    data[0+2*ii] -= I2S_32::DC_left;
    data[1+2*ii] -= I2S_32::DC_right;
  }

  int32_t dco0=I2S_32::DC_left;
  int32_t dco1=I2S_32::DC_right;
  I2S_32::DC_left  = (NAVG * dco0 +(dc0-dco0))/NAVG;
  I2S_32::DC_right = (NAVG * dco1 +(dc0-dco1))/NAVG;
*/

   // extract 16/32 bit from 32 bit I2S buffer but shift to right first
   // there will be two buffers with each having "AUDIO_BLOCK_SAMPLES_NCH" samples
  left  = I2S_32::block_left;
  right = I2S_32::block_right;
  if (left != NULL && right != NULL) {
    offset = I2S_32::block_offset;
    if (offset <= AUDIO_BLOCK_SAMPLES/2) {
      dest_left  = &(left->data[offset]);
      dest_right = &(right->data[offset]);
      I2S_32::block_offset = offset + AUDIO_BLOCK_SAMPLES/2; 
      do { 
        *dest_left++  = (*src++)>>I2S_32::shift; // left side may be 16 or 32 bit
        *dest_right++ = (*src++)>>I2S_32::shift;
      } while (src < end);
    }
  }
}

void I2S_32::update(void)
{
  audio_block_t *new_left=NULL, *new_right=NULL, *out_left=NULL, *out_right=NULL;

  // allocate 2 new blocks, but if one fails, allocate neither
  new_left = allocate();
  if (new_left != NULL) {
    new_right = allocate();
    if (new_right == NULL) {
      release(new_left);
      new_left = NULL;
    }
  }
  __disable_irq();
  if (block_offset >= AUDIO_BLOCK_SAMPLES) {
    // the DMA filled 2 blocks, so grab them and get the
    // 2 new blocks to the DMA, as quickly as possible

    out_left = block_left;
    block_left = new_left;
    out_right = block_right;
    block_right = new_right;
    block_offset = 0;
    __enable_irq();
    
    // then transmit the DMA's former blocks
    transmit(out_left, 0);
    release(out_left);
    transmit(out_right, 1);
    release(out_right);
  } else if (new_left != NULL) {
    // the DMA didn't fill blocks, but we allocated blocks
    if (block_left == NULL) {
      // the DMA doesn't have any blocks to fill, so
      // give it the ones we just allocated
      block_left = new_left;
      block_right = new_right;
      block_offset = 0;
      __enable_irq();
    } else {
      // the DMA already has blocks, doesn't need these
      __enable_irq();
      release(new_left);
      release(new_right);
    }
  } else {
    // The DMA didn't fill blocks, and we could not allocate
    // memory... the system is likely starving for memory!
    // Sadly, there's nothing we can do.
    __enable_irq();
  }
}

#if defined(KINETISK)
// MCLK needs to be 48e6 / 1088 * 256 = 11.29411765 MHz -> 44.117647 kHz sample rate
//
#if F_CPU == 96000000 || F_CPU == 48000000 || F_CPU == 24000000
  // PLL is at 96 MHz in these modes
  #define MCLK_MULT 2
  #define MCLK_DIV  17
#elif F_CPU == 72000000
  #define MCLK_MULT 8
  #define MCLK_DIV  51
#elif F_CPU == 120000000
  #define MCLK_MULT 8
  #define MCLK_DIV  85
#elif F_CPU == 144000000
  #define MCLK_MULT 4
  #define MCLK_DIV  51
#elif F_CPU == 168000000
  #define MCLK_MULT 8
  #define MCLK_DIV  119
#elif F_CPU == 180000000
  #define MCLK_MULT 16
  #define MCLK_DIV  255
  #define MCLK_SRC  0
#elif F_CPU == 192000000
  #define MCLK_MULT 1
  #define MCLK_DIV  17
#elif F_CPU == 216000000
  #define MCLK_MULT 12
  #define MCLK_DIV  17
  #define MCLK_SRC  1
#elif F_CPU == 240000000
  #define MCLK_MULT 2
  #define MCLK_DIV  85
  #define MCLK_SRC  0
#elif F_CPU == 256000000
  #define MCLK_MULT 12
  #define MCLK_DIV  17
  #define MCLK_SRC  1
#elif F_CPU == 16000000
  #define MCLK_MULT 12
  #define MCLK_DIV  17
#else
  #error "This CPU Clock Speed is not supported by the Audio library";
#endif

#ifndef MCLK_SRC
  #if F_CPU >= 20000000
    #define MCLK_SRC  3  // the PLL
  #else
    #define MCLK_SRC  0  // system clock
  #endif
#endif
void I2S_32::config_i2s(void)
{
  SIM_SCGC6 |= SIM_SCGC6_I2S;
  SIM_SCGC7 |= SIM_SCGC7_DMA;
  SIM_SCGC6 |= SIM_SCGC6_DMAMUX;

  // if either transmitter or receiver is enabled, do nothing
  if (I2S0_TCSR & I2S_TCSR_TE) return;
  if (I2S0_RCSR & I2S_RCSR_RE) return;

  // enable MCLK output
  I2S0_MCR = I2S_MCR_MICS(MCLK_SRC) | I2S_MCR_MOE;
  while (I2S0_MCR & I2S_MCR_DUF) ;
  I2S0_MDR = I2S_MDR_FRACT((MCLK_MULT-1)) | I2S_MDR_DIVIDE((MCLK_DIV-1));

  // configure transmitter
  I2S0_TMR = 0;
  I2S0_TCR1 = I2S_TCR1_TFW(1);  // watermark at half fifo size
  I2S0_TCR2 = I2S_TCR2_SYNC(0) | I2S_TCR2_BCP | I2S_TCR2_MSEL(1)
    | I2S_TCR2_BCD | I2S_TCR2_DIV(1);
  I2S0_TCR3 = I2S_TCR3_TCE;
  I2S0_TCR4 = I2S_TCR4_FRSZ(1) | I2S_TCR4_SYWD(31) | I2S_TCR4_MF
    | I2S_TCR4_FSE | I2S_TCR4_FSP | I2S_TCR4_FSD;
  I2S0_TCR5 = I2S_TCR5_WNW(31) | I2S_TCR5_W0W(31) | I2S_TCR5_FBT(31);

  // configure receiver (sync'd to transmitter clocks)
  I2S0_RMR = 0;
  I2S0_RCR1 = I2S_RCR1_RFW(1);
  I2S0_RCR2 = I2S_RCR2_SYNC(1) | I2S_TCR2_BCP | I2S_RCR2_MSEL(1)
    | I2S_RCR2_BCD | I2S_RCR2_DIV(1);
  I2S0_RCR3 = I2S_RCR3_RCE;
  I2S0_RCR4 = I2S_RCR4_FRSZ(1) | I2S_RCR4_SYWD(31) | I2S_RCR4_MF
    | I2S_RCR4_FSE | I2S_RCR4_FSP | I2S_RCR4_FSD;
  I2S0_RCR5 = I2S_RCR5_WNW(31) | I2S_RCR5_W0W(31) | I2S_RCR5_FBT(31);

  // configure pin mux for 3 clock signals
  CORE_PIN23_CONFIG = PORT_PCR_MUX(6); // pin 23, PTC2, I2S0_TX_FS (LRCLK)
  CORE_PIN9_CONFIG  = PORT_PCR_MUX(6); // pin  9, PTC3, I2S0_TX_BCLK
  CORE_PIN11_CONFIG = PORT_PCR_MUX(6); // pin 11, PTC6, I2S0_MCLK
}

#elif defined (__IMXRT1062__)

//#define AUDIO_SAMPLE_RATE_EXACT 44100 // used for initialization

void I2S_32::config_i2s(void)
{
	CCM_CCGR5 |= CCM_CCGR5_SAI1(CCM_CCGR_ON);

	// if either transmitter or receiver is enabled, do nothing
	if (I2S1_TCSR & I2S_TCSR_TE) return;
	if (I2S1_RCSR & I2S_RCSR_RE) return;
//PLL:
	int fs = AUDIO_SAMPLE_RATE_EXACT;
 
  setAudioFrequency(fs);
  /*
	// PLL between 27*24 = 648MHz und 54*24=1296MHz
	int n1 = 4; //SAI prescaler 4 => (n1*n2) = multiple of 4
	int n2 = 1 + (24000000 * 27) / (fs * 256 * n1);

	double C = ((double)fs * 256 * n1 * n2) / 24000000;
	int c0 = C;
	int c2 = 10000;
	int c1 = C * c2 - (c0 * c2);
	set_audioClock(c0, c1, c2);

	// clear SAI1_CLK register locations
	CCM_CSCMR1 = (CCM_CSCMR1 & ~(CCM_CSCMR1_SAI1_CLK_SEL_MASK))
		   | CCM_CSCMR1_SAI1_CLK_SEL(2); // &0x03 // (0,1,2): PLL3PFD0, PLL5, PLL4
	CCM_CS1CDR = (CCM_CS1CDR & ~(CCM_CS1CDR_SAI1_CLK_PRED_MASK | CCM_CS1CDR_SAI1_CLK_PODF_MASK))
		   | CCM_CS1CDR_SAI1_CLK_PRED(n1-1) // &0x07
		   | CCM_CS1CDR_SAI1_CLK_PODF(n2-1); // &0x3f
	// Select MCLK
	IOMUXC_GPR_GPR1 = (IOMUXC_GPR_GPR1
		& ~(IOMUXC_GPR_GPR1_SAI1_MCLK1_SEL_MASK))
		| (IOMUXC_GPR_GPR1_SAI1_MCLK_DIR | IOMUXC_GPR_GPR1_SAI1_MCLK1_SEL(0));
*/

	CORE_PIN23_CONFIG = 3;  //1:MCLK
	CORE_PIN21_CONFIG = 3;  //1:RX_BCLK
	CORE_PIN20_CONFIG = 3;  //1:RX_SYNC

	int rsync = 0;
	int tsync = 1;

	I2S1_TMR = 0;
	//I2S1_TCSR = (1<<25); //Reset
	I2S1_TCR1 = I2S_TCR1_RFW(1);
	I2S1_TCR2 = I2S_TCR2_SYNC(tsync) | I2S_TCR2_BCP // sync=0; tx is async;
		    | (I2S_TCR2_BCD | I2S_TCR2_DIV((1)) | I2S_TCR2_MSEL(1));
	I2S1_TCR3 = I2S_TCR3_TCE;
	I2S1_TCR4 = I2S_TCR4_FRSZ((2-1)) | I2S_TCR4_SYWD((32-1)) | I2S_TCR4_MF
		    | I2S_TCR4_FSE | I2S_TCR4_FSP | I2S_TCR4_FSD;
	I2S1_TCR5 = I2S_TCR5_WNW((32-1)) | I2S_TCR5_W0W((32-1)) | I2S_TCR5_FBT((32-1));

	I2S1_RMR = 0;
	//I2S1_RCSR = (1<<25); //Reset
	I2S1_RCR1 = I2S_RCR1_RFW(1);
	I2S1_RCR2 = I2S_RCR2_SYNC(rsync) | I2S_RCR2_BCP  // sync=0; rx is async;
  	    | (I2S_RCR2_BCD | I2S_RCR2_DIV((1)) | I2S_RCR2_MSEL(1));
	I2S1_RCR3 = I2S_RCR3_RCE;
	I2S1_RCR4 = I2S_RCR4_FRSZ((2-1)) | I2S_RCR4_SYWD((32-1)) | I2S_RCR4_MF
		    | I2S_RCR4_FSE | I2S_RCR4_FSP | I2S_RCR4_FSD;
	I2S1_RCR5 = I2S_RCR5_WNW((32-1)) | I2S_RCR5_W0W((32-1)) | I2S_RCR5_FBT((32-1));
}
#endif

#endif
