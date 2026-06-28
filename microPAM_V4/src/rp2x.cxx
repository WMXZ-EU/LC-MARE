/* microPAM 
 * Copyright (c) 2026, Walter Zimmer
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
 
#include "Arduino.h"
#include "global.h"

#if (MCU==RP_2040) || (MCU==RP_2350)
#include "rp2x.h"
#include "adc.h"
#include "rtc.h"
#include "process.h"

  void set_MCU_clock(int32_t mcu_factor)
  {
    set_sys_clock_khz(mcu_factor*12000, true);
  }
  
  void printCrashReport(void) {}

  /*************************** TDM ****************************************************/
  #define I2S_DOUT  11
  #define I2S_BCLK  9
  #define I2S_FSYNC (I2S_BCLK+1) // must always be this way

  #define MBIT 32
  #define NCH NCHAN_I2S

  pin_size_t _pinDOUT =I2S_DOUT;
  pin_size_t _pinBCLK =I2S_BCLK;

  int _bps =MBIT;
  int off=0;

  PIOProgram *_i2s;
  static PIO _pio;
  static int _sm;

  #include "hardware/pio.h"

  // ---------- //
  // pio_tdm_in //
  // ---------- //
  // custom macros
  #define SETX(SIDE,VAL)  ((7<<13)| (SIDE<<11)| (1<<5)| (VAL))
  #define SETY(SIDE,VAL)  ((7<<13)| (SIDE<<11)| (2<<5)| (VAL))
  #define JMPX(SIDE,VAL)  ((0<<13)| (SIDE<<11)| (2<<5)| (VAL))
  #define JMPY(SIDE,VAL)  ((0<<13)| (SIDE<<11)| (4<<5)| (VAL))
  #define NOP(SIDE)       ((5<<13)| (SIDE<<11)| (2<<5)| (2))
  #define INP(SIDE,VAL)   ((2<<13)| (SIDE<<11)| (0<<5)| (VAL))  // input pin
  #define INN(SIDE,VAL)   ((2<<13)| (SIDE<<11)| (3<<5)| (VAL))  // input zero

  #if NCHAN_I2S==1
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
  
  #elif NCHAN_I2S>=2
    #define pio_tdm_in_wrap_target 0
    #define pio_tdm_in_wrap 11

    // following is for multi channel TDM
    static const uint16_t pio_tdm_in_program_instructions[] = {
        SETY(0b11,NCHAN_I2S-2),
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
      .length = (pio_tdm_in_wrap+1),
      .origin = -1,
  };

  static inline pio_sm_config pio_tdm_in_program_get_default_config(uint offset) {
      pio_sm_config c = pio_get_default_sm_config();
      sm_config_set_wrap(&c, offset + pio_tdm_in_wrap_target, offset + pio_tdm_in_wrap);
      sm_config_set_sideset(&c, 2, false, false);
      return c;
  }

  static inline void pio_tdm_in_program_init(PIO pio, uint sm, uint offset, uint data_pin, uint clock_pin_base, uint bits) 
  {
      pio_gpio_init(pio, data_pin);
      pio_gpio_init(pio, clock_pin_base);
      pio_gpio_init(pio, clock_pin_base + 1);
      //
      pio_sm_config sm_config = pio_tdm_in_program_get_default_config(offset);
      //
      sm_config_set_in_pins(&sm_config, data_pin);
      sm_config_set_sideset_pins(&sm_config, clock_pin_base);
      sm_config_set_in_shift(&sm_config, false, true,  bits);
      sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_RX);
      //
      pio_sm_init(pio, sm, offset, &sm_config);
      //
      uint pin_mask = 3u << clock_pin_base;
      pio_sm_set_pindirs_with_mask(pio, sm, pin_mask, pin_mask);
      pio_sm_set_pins(pio, sm, 0); // clear pins
      //
      //pio_sm_exec(pio, sm, pio_encode_set(pio_y, bits - 2));
  }

  void acqModifyFrequency(uint32_t fsamp)
  { 
    float bitClk = fsamp * _bps * NCH /* SAI channels */ * 2.0 /* edges per clock */;
    pio_sm_set_enabled(_pio, _sm, false);
    pio_sm_set_clkdiv(_pio, _sm, (float)clock_get_hz(clk_sys) / bitClk);
    pio_sm_set_enabled(_pio, _sm, true);
  }

  void i2s_setup(uint32_t fsamp)
  {
    _i2s = new PIOProgram( &pio_tdm_in_program);
    //
    _i2s->prepare(&_pio, &_sm, &off);

    pio_tdm_in_program_init(_pio, _sm, off, _pinDOUT, _pinBCLK, _bps);

    acqModifyFrequency(fsamp); // Will also start I2S
  }

  void i2s_start(void) { pio_sm_set_enabled(_pio, _sm, true); }
  void i2s_stop(void)  { pio_sm_set_enabled(_pio, _sm, false); }
  
  /*------------------------------DMA ...........................................*/
  int _channelDMA[2];
  int32_t i2s_buffer[3][NBUF_I2S];
  int _wordsPerBuffer=NBUF_I2S;

  static void __not_in_flash_func(dma_irq)(void);

  extern void process_acq_init();
  extern void __not_in_flash_func(process_acq)(int32_t * out, int32_t *inp, int32_t nbuf);
  extern int32_t *__not_in_flash_func(compressData)(int32_t *buffer);
   
  void dma_setup(void)
  {
      for (auto ii = 0; ii < 2; ii++) _channelDMA[ii] = dma_claim_unused_channel(true);

      int dreq;
      dreq = pio_get_dreq(_pio, _sm, false);
      volatile void *pioFIFOAddr =(volatile void*)&_pio->rxf[_sm];

      for (auto ii = 0; ii < 2; ii++) 
      {  
        dma_channel_config c;
        c = dma_channel_get_default_config(_channelDMA[ii]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32); // 16b/32b transfers into PIO FIFO
        channel_config_set_read_increment(&c, false); // Reading same FIFO address
        channel_config_set_write_increment(&c, true); // Writing to incrememting buffers

        channel_config_set_dreq(&c, dreq); // Wait for the PIO TX FIFO specified
        channel_config_set_chain_to(&c, _channelDMA[ii ^ 1]); // Start other channel when done
        channel_config_set_irq_quiet(&c, false); // Need IRQs

        dma_channel_configure(_channelDMA[ii], &c, i2s_buffer[ii], pioFIFOAddr, _wordsPerBuffer , false);

        dma_channel_set_irq0_enabled(_channelDMA[ii], true);
      }

      //irq_add_shared_handler(DMA_IRQ_0, dma_irq, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
      irq_add_shared_handler(DMA_IRQ_0, dma_irq, 0x60); // 0x80 is DEFAULT
      irq_set_enabled(DMA_IRQ_0, true);

      dma_channel_start(_channelDMA[0]);
  }
  
  void dma_exit(void)
  {
    irq_set_enabled(DMA_IRQ_0, false);
  }

  void process(int32_t * buffer);

  static void __not_in_flash_func(dma_irq)(void)
  {
    for(int ii=0; ii<2; ii++)
    if(dma_channel_get_irq0_status(_channelDMA[ii]))
    { //
      dma_channel_acknowledge_irq0(_channelDMA[ii]);
      memcpy(i2s_buffer[2], i2s_buffer[ii], 4*NBUF_I2S); // copy data from DMA buffer
      //
      dma_channel_set_write_addr(_channelDMA[ii], i2s_buffer[ii], false);
      dma_channel_set_trans_count(_channelDMA[ii], _wordsPerBuffer, false);

      process(i2s_buffer[2]);
      return;
    }
  }

  void acqInit(uint32_t fs)
  {
    i2s_setup(fs);
    dma_setup();
  }

  void acqStart(void)
  {
    i2s_start();
    adc_init();
  }

  void acqStop(void)
  {
    i2s_stop();
    adc_exit();
  }

  /*-------------misc---------------------*/
  //extern void SD_stop(void);
  void stopSystem(void)
  { //
    //SD_stop();
    delay(100);
  }

  void lowPowerInit(void) {}

  void systemInit(void)
  { lowPowerInit();
    neo_pixel_init();
  }
  void usbPowerSetup(void) {}
  void usbPowerOff(void) {}
  void usbPowerExit(void) {}

/*---------------------------------------- Hibernate ------------------------------------*/
  #include "pico/stdlib.h"

  #include "pico.h"

  #include "pico/runtime_init.h"
  #include "hardware/pll.h"
  #include "hardware/regs/clocks.h"
  #include "hardware/clocks.h"
  #include "hardware/xosc.h"
  #include "hardware/structs/rosc.h"

  #if MCU != RP_2040
      #include "hardware/powman.h"
  #endif

  void usb_stop(void)
  {
      clock_stop(clk_usb);
  }
  
  inline static void rosc_clear_bad_write(void) {
      hw_clear_bits(&rosc_hw->status, ROSC_STATUS_BADWRITE_BITS);
  }
  inline static bool rosc_write_okay(void) {
      return !(rosc_hw->status & ROSC_STATUS_BADWRITE_BITS);
  }
  inline static void rosc_write(io_rw_32 *addr, uint32_t value) {
      rosc_clear_bad_write();
      assert(rosc_write_okay());
      *addr = value;
      assert(rosc_write_okay());
  };

  void rosc_disable(void) {
      uint32_t tmp = rosc_hw->ctrl;
      tmp &= (~ROSC_CTRL_ENABLE_BITS);
      tmp |= (ROSC_CTRL_ENABLE_VALUE_DISABLE << ROSC_CTRL_ENABLE_LSB);
      rosc_write(&rosc_hw->ctrl, tmp);
      // Wait for stable to go away
      while(rosc_hw->status & ROSC_STATUS_STABLE_BITS);
  }

  void rosc_enable(void) {
      //Re-enable the rosc
      rosc_write(&rosc_hw->ctrl, ROSC_CTRL_ENABLE_BITS);

      //Wait for it to become stable once restarted
      while (!(rosc_hw->status & ROSC_STATUS_STABLE_BITS));
  }

  // use only xtal oscillator (12 MHz clock)
  void sleep_run_from_xosc(void) 
  {
      uint src_hz;
      uint clk_ref_src;
      src_hz = XOSC_HZ;
      clk_ref_src = CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC;

      // CLK_REF = XOSC
      clock_configure(clk_ref,
                      clk_ref_src,
                      0, // No aux mux
                      src_hz,
                      src_hz);

      // CLK SYS = CLK_REF
      clock_configure(clk_sys,
                      CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                      0, // Using glitchless mux
                      src_hz,
                      src_hz);

      // CLK ADC = 0MHz
      clock_stop(clk_adc);
      clock_stop(clk_usb);
      #if MCU==RP_2350
          clock_stop(clk_hstx);
      #endif      
      #if HAS_RP2040_RTC
        clock_stop(clk_rtc);
      #endif
          // CLK PERI = clk_sys. Used as reference clock for Peripherals. No dividers so just select and enable
      clock_configure(clk_peri,
                      0,
                      CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                      src_hz,
                      src_hz);

      pll_deinit(pll_sys);
      pll_deinit(pll_usb);

      // Can disable rosc
      rosc_disable();
  }
  
  void sleep_goto_dormant_until_pin(uint32_t gpio_pin) 
  {
      uint32_t event = 0;
      event = IO_BANK0_DORMANT_WAKE_INTE0_GPIO0_LEVEL_LOW_BITS;

      gpio_init(gpio_pin);
      gpio_set_input_enabled(gpio_pin, true);
      gpio_set_dormant_irq_enabled(gpio_pin, event, true);

      // disable systick now so that no milisecond interrupts will occur
      //systick_hw->csr &= ~1;
      // we will get out of sleep when an interrupt occurs.
  
      xosc_dormant();
      // Execution stops here until woken up

      // Clear the irq so we can go back to dormant mode again if we want
      gpio_acknowledge_irq(gpio_pin, event);
      gpio_set_input_enabled(gpio_pin, false);

      //systick_hw->csr |= 1; // enable systick again, hope we survived this
      // we don't actually know the time duration during which we were dormant.
      // so, the absolute value ofmillis() will be messed up.

  }

  // To be called after waking up from sleep/dormant mode to restore system clocks properly
  void sleep_power_up(void)
  {
      // Re-enable the ring oscillator, which will essentially kickstart the proc
      rosc_enable();

      // Reset the sleep enable register so peripherals and other hardware can be used
      clocks_hw->sleep_en0 |= ~(0u);
      clocks_hw->sleep_en1 |= ~(0u);

      // Restore all clocks
      clocks_init();

      #if MCU==RP_2350
          // make powerman use xosc again
          uint64_t restore_ms = powman_timer_get_ms();
          powman_timer_set_1khz_tick_source_xosc();
          powman_timer_set_ms(restore_ms);
      #endif
  }

  void doReboot(void){ rp2040.restart(); }

  void doReset()
  {
    #define AIRCR_Register (*((volatile uint32_t*)(PPB_BASE + 0x0ED0C)))
    AIRCR_Register = 0x5FA0004;    
    //{ *(uint32_t *)0xE000ED0C =  0x5FA0004;}    // from Teensy
  }

  void goDormant(void) 
  {
    // 'switch-off' all I/O pins
    for(uint16_t p=0;p<PINS_COUNT;p++)
    //if(p != XRTC_INT_PIN)
    { pinMode(p, INPUT); // best performance!
      gpio_set_input_enabled(p, false); // disable input gate
    }

    sleep_run_from_xosc();
    #if HAS_RP2040_RTC
      clock_stop(clk_rtc);
    #endif
    sleep_goto_dormant_until_pin(XRTC_INT_PIN);
    //
    // will resume action here
    sleep_power_up();
    // simply restart program to facilitate setup
    doReboot();
  }

  extern char startTime[];

  void setAlarm(uint32_t sec)
  { XRTCsetAlarm(sec);
      datetime_t tm;
      time2date(sec, &tm, 2000);
      encodeTimestamp(startTime,&tm);
  }

  uint32_t getPSRAMSize(void) {return rp2040.getPSRAMSize(); }

  /************************************ UID **********************************************/
  #include "pico/unique_id.h"
  char uid_strng[10]; 
  void getUID(void)
  {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    Serial.print("unique_board_id: ");
    for (int len = 0; len < 8; len++) 
    { Serial.print(id.id[len],HEX); Serial.print(' '); 
    } 
    Serial.println();  
    sprintf(uid_strng,"%02X%02X%02X%02X",id.id[4],id.id[5],id.id[6],id.id[7]);
  }

  /************************************ NEO Pixel ****************************************/
  #include <Adafruit_NeoPixel.h>
  Adafruit_NeoPixel neo_pixel(1, PIN_NEOPIXEL, NEO_GRB);

  void neo_pixel_init()
  {
    neo_pixel.begin();
    neo_pixel.clear();
  }

  void neo_pixel_show(uint16_t r, uint16_t g, uint16_t b)
  {
    neo_pixel.setPixelColor(0, neo_pixel.Color(r, g, b));
    neo_pixel.show();
  }

  #if MCU==RP_2040
      #include "hardware/rtc.h"

    uint32_t rtc_get(void)
    { // get seconds since epoch time
      datetime_t tm;
      // get local time
      rtc_get_datetime(&tm);    
      return date2time(&tm, 2000);
    }

    void rtc_set(uint32_t tt)
    {
      datetime_t tm;
      time2date(tt,&tm,2000);
      // set local time
      rtc_set_datetime(&tm);
    }

  #elif MCU==RP_2350
    #include "pico/aon_timer.h"
    #include "pico/util/datetime.h"
    #include "pico/stdlib.h"

    //bool rtc_get_datetime(datetime_t *t);
    //bool rtc_set_datetime(const datetime_t *t);

    struct timespec ts= {0,0};
    void rtc_init(void) 
    { aon_timer_start(&ts);
    }

    bool rtc_running(void)
    { return aon_timer_is_running();}

    bool rtc_get_datetime(datetime_t *t)
    {
      time2date(rtc_get(), t, 2000);
      return 1;
    }

    bool rtc_set_datetime(const datetime_t *t)
    {
      rtc_set(date2time(t,2000));
      return 1;
    }

    uint32_t rtc_get(void)
    { struct timespec ts;
      aon_timer_get_time(&ts);
      return ts.tv_sec;
    }

    void rtc_set(uint32_t tt)
    { struct timespec ts = {tt,0};
      aon_timer_set_time(&ts);
    }

  #endif

#endif