#include "Arduino.h"
#include "global.h"

#if MCU==T_4_1
  #include "teensy.h"
  #include "acq.h"
  #include "rtc.h"
  #include "process.h"

    /*******************************************************************************/
  uint32_t getPSRAMSize(void) {return 0;}


    // use usb host 5V power (has 100uF capacitor)
    void usbPowerInit()
    {
      IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_40 = 5;
      IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_40 = 0x0008; // slow speed, weak 150 ohm drive
      GPIO8_GDIR |= 1<<26;
    }
    void usbPowerExit()
    {
      IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_40 = 0; // disable
      GPIO8_GDIR &= ~(1<<26);
    }

    void usbPowerOn()  { GPIO8_DR_SET = 1<<26; }
    void usbPowerOff() { GPIO8_DR_CLEAR = 1<<26; }

    void usbPowerSetup(void)
    {
      usbPowerInit();
      usbPowerOn();
      delay(100);
    }

  /****************misc**********************************************************/
  extern "C" uint32_t set_arm_clock(uint32_t frequency); // clockspeed.c
  void set_MCU_clock(int32_t mcu_factor)
  {
    set_arm_clock(mcu_factor*12000000);
  }

  void lowPowerInit(void)
  { // keep memory powered during sleep
    CCM_CGPR |= CCM_CGPR_INT_MEM_CLK_LPM;
    // keep cpu clock on in wait mode (required for systick to trigger wake-up)
    CCM_CLPCR &= ~(CCM_CLPCR_ARM_CLK_DIS_ON_LPM | CCM_CLPCR_LPM(3));
    // set SoC low power mode to wait mode
    CCM_CLPCR |= CCM_CLPCR_LPM(1);
    // ensure above config is done before executing WFI
    asm volatile("dsb");
  }

/************************************ADC***************************************************/

  #define MSYNC   1

  #define MBIT      32      // number of bits / sample from ADC
  #define MDIV       1      // MCLK divider (MCLK = 2*MDIV*BCLK)

  #define NCH NCHAN_I2S

  void process(int32_t * buffer);

  PROGMEM
  void set_audioClock(int nfact, int32_t nmult, uint32_t ndiv) // sets PLL4
  {
    CCM_ANALOG_PLL_AUDIO = CCM_ANALOG_PLL_AUDIO_BYPASS | CCM_ANALOG_PLL_AUDIO_ENABLE
            | CCM_ANALOG_PLL_AUDIO_POST_DIV_SELECT(2) // 2: 1/4; 1: 1/2; 0: 1/1
            | CCM_ANALOG_PLL_AUDIO_DIV_SELECT(nfact);

    CCM_ANALOG_PLL_AUDIO_NUM   = nmult & CCM_ANALOG_PLL_AUDIO_NUM_MASK;
    CCM_ANALOG_PLL_AUDIO_DENOM = ndiv & CCM_ANALOG_PLL_AUDIO_DENOM_MASK;
    
    CCM_ANALOG_PLL_AUDIO &= ~CCM_ANALOG_PLL_AUDIO_POWERDOWN;  //Switch on PLL
    while (!(CCM_ANALOG_PLL_AUDIO & CCM_ANALOG_PLL_AUDIO_LOCK)) {}; //Wait for pll-lock
    
    const int div_post_pll = 1; // other values: 2,4
    CCM_ANALOG_MISC2 &= ~(CCM_ANALOG_MISC2_DIV_MSB | CCM_ANALOG_MISC2_DIV_LSB);
    if(div_post_pll>1) CCM_ANALOG_MISC2 |= CCM_ANALOG_MISC2_DIV_LSB;
    if(div_post_pll>3) CCM_ANALOG_MISC2 |= CCM_ANALOG_MISC2_DIV_MSB;
    
    CCM_ANALOG_PLL_AUDIO &= ~CCM_ANALOG_PLL_AUDIO_BYPASS;   //Disable Bypass
  }

  void setAudioFrequency(int fs)
  {
    int ovr = 2*MDIV*(NCHAN_I2S*32);
    Serial.print("ovr: "); Serial.println(ovr);

    // PLL between 27*24 = 648MHz und 54*24=1296MHz
    int n0 = 26; // targeted PLL frequency (n0*24 MHz) n0>=27 && n0<54
    int n1, n2;
    do
    {   n0++;
        n1=0;
        do
        {   n1++; 
            n2 = 1 + (24'000'000 * n0) / (fs * ovr * n1);
        } while ((n2>64) && (n1<=8));
    } while ((n2>64 && n0<54));
    Serial.printf("fs=%d, no=%d, n1=%d, n2=%d\r\n", fs, n0,n1,n2);

    double C = ((double)fs * ovr * n1 * n2) / 24000000;
    Serial.print("C: "); Serial.println(C);

    int c0 = C;
    int c2 = 10'000;
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

  }

  void i2s_setup(uint32_t fsamp)
  {
    CCM_CCGR5 |= CCM_CCGR5_SAI1(CCM_CCGR_ON);

    // if receiver is enabled, do nothing
    if (I2S1_RCSR & I2S_RCSR_RE) return;
    //PLL:
    int fs = fsamp;
  
    setAudioFrequency(fs);

    CORE_PIN23_CONFIG = 3;  //1:MCLK
    CORE_PIN21_CONFIG = 3;  //1:RX_BCLK
    CORE_PIN20_CONFIG = 3;  //1:RX_SYNC

  	CORE_PIN8_CONFIG  = 3;  //1:RX_DATA0
  	IOMUXC_SAI1_RX_DATA0_SELECT_INPUT = 2;

    I2S1_RMR = 0;
    //I2S1_RCSR = (1<<25); //Reset
    I2S1_RCR1 = I2S_RCR1_RFW(4);
    I2S1_RCR2 = I2S_RCR2_SYNC(0) //| I2S_RCR2_BCP  
              | (I2S_RCR2_BCD | I2S_RCR2_DIV((MDIV-1)) | I2S_RCR2_MSEL(1));
    I2S1_RCR3 = I2S_RCR3_RCE;
    I2S1_RCR4 = I2S_RCR4_FRSZ((NCHAN_I2S-1)) | I2S_RCR4_SYWD((MSYNC-1)) | I2S_RCR4_MF
              | I2S_RCR4_FSE | I2S_RCR4_FSP | I2S_RCR4_FSD;
    I2S1_RCR5 = I2S_RCR5_WNW((MBIT-1)) | I2S_RCR5_W0W((MBIT-1)) | I2S_RCR5_FBT((MBIT-1));

    I2S1_RCSR = I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR;
  }

  /******************************************************************************************/
  #include "DMAChannel.h"

  static DMAChannel dma;
  DMAMEM  __attribute__((aligned(32))) static  uint32_t i2s_buffer[2*NBUF_I2S];
  
  static void acq_isr(void);

  void dma_setup(void)
  {
    dma.begin(true); // Allocate the DMA channel first

    dma.TCD->SADDR = (void *)((uint32_t)&I2S1_RDR0);
    dma.TCD->SOFF = 0;
    dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE((MBIT/16)) | DMA_TCD_ATTR_DSIZE((MBIT/16));
    dma.TCD->NBYTES_MLNO = (MBIT/8);
    dma.TCD->SLAST = 0;
    dma.TCD->DADDR = i2s_buffer;
    dma.TCD->DOFF = (MBIT/8);
    dma.TCD->CITER_ELINKNO = 2*NBUF_I2S;
    dma.TCD->DLASTSGA = -sizeof(i2s_buffer);
    dma.TCD->BITER_ELINKNO = dma.TCD->CITER_ELINKNO;
    dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;

    dma.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI1_RX);

    dma.attachInterrupt(acq_isr, 0x60);	
    dma.enable();
  }

  #define IMXRT_CACHE_ENABLED 2 // 0=disabled, 1=WT, 2= WB
  static void acq_isr(void)
  {
    uint32_t daddr;
    int32_t *src;
  
    daddr = (uint32_t)(dma.TCD->DADDR);

    dma.clearInterrupt();
  
    if (daddr < (uint32_t) &i2s_buffer[NBUF_I2S]) 
    {
      // DMA is receiving to the first half of the buffer
      // need to remove data from the second half
      src = (int32_t *)&i2s_buffer[NBUF_I2S];
    }
    else
    {
    // DMA is receiving to the second half of the buffer
    // need to remove data from the first half
      src = (int32_t *)&i2s_buffer[0];
    }

    #if IMXRT_CACHE_ENABLED >=1
        arm_dcache_delete((void*)src, sizeof(i2s_buffer) / 2);
    #endif

    process(src);
  }

  /*-------------Utilities----------------------------*/
  void acqModifyFrequency(uint32_t fsamp)
  {
    // stop I2S
    I2S1_RCSR &= ~(I2S_RCSR_RE | I2S_RCSR_BCE);
    setAudioFrequency(fsamp);
    //restart I2S
    I2S1_RCSR |= I2S_RCSR_RE | I2S_RCSR_BCE;
  }

  void i2s_start(void){   I2S1_RCSR |=  (I2S_RCSR_RE | I2S_RCSR_BCE);}
  void  i2s_stop(void){   I2S1_RCSR &= ~(I2S_RCSR_RE | I2S_RCSR_BCE);}

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

  void dma_exit(void){}


    /*---------------hibernate -------------------*/
   void doReset(void)  { *(uint32_t *)0xE000ED0C =  0x5FA0004; }
   void reboot(void) { *(uint32_t *)0xE000ED0C =  0x5FA0004;}
    void powerDown(void)
    {
      SNVS_LPCR |= (1 << 6); // turn off power
      while (1) asm("wfi");      
    }

    void stopSystem(void)
    { //shutting down power
    //stopSD(); 
    adc_exit();
    //stopUSB();
    }

    #define SNVS_LPCR_LPTA_EN_MASK    (0x2U)

    void doHibernate(uint32_t secs)
    {
        uint32_t tmp = SNVS_LPCR; // save control register
        SNVS_LPSR |= 1;
        asm volatile("DSB");

        // disable alarm
        SNVS_LPCR &= ~SNVS_LPCR_LPTA_EN_MASK;
        while (SNVS_LPCR & SNVS_LPCR_LPTA_EN_MASK);

        __disable_irq();

        //get Time:
        uint32_t lsb, msb;
        do {
          msb = SNVS_LPSRTCMR;
          lsb = SNVS_LPSRTCLR;
        } while ( (SNVS_LPSRTCLR != lsb) | (SNVS_LPSRTCMR != msb) );
        uint32_t secso = (msb << 17) | (lsb >> 15);

        // if alarm is not in future do not hibernate
        if(secs <= secso) return;

        //set alarm
        SNVS_LPTAR = secs;
        while (SNVS_LPTAR != secs);

        // restore control register and set alarm
        SNVS_LPCR = tmp | SNVS_LPCR_LPTA_EN_MASK; 
        while (!(SNVS_LPCR & SNVS_LPCR_LPTA_EN_MASK));

    //    NVIC_CLEAR_PENDING(IRQ_SNVS_ONOFF);
    //    attachInterruptVector(IRQ_SNVS_ONOFF, &call_back);
    //    NVIC_SET_PRIORITY(IRQ_SNVS_ONOFF, 255); //lowest priority
    //    asm volatile ("dsb"); //make sure to write before interrupt-enable
    //    NVIC_ENABLE_IRQ(IRQ_SNVS_ONOFF);
        __enable_irq();
      
        SNVS_LPCR |= (1 << 6); // turn off power
        while (1) asm("wfi");  
    }


  extern char startTime[];

    void setAlarm(uint32_t secs)
    {
        uint32_t tmp = SNVS_LPCR; // save control register
        SNVS_LPSR |= 1;
        asm volatile("DSB");

        // disable alarm
        SNVS_LPCR &= ~SNVS_LPCR_LPTA_EN_MASK;
        while (SNVS_LPCR & SNVS_LPCR_LPTA_EN_MASK);

        __disable_irq();
        //set alarm
        SNVS_LPTAR = secs;
        while (SNVS_LPTAR != secs);

        // restore control register and set alarm
        SNVS_LPCR = tmp | SNVS_LPCR_LPTA_EN_MASK; 
        while (!(SNVS_LPCR & SNVS_LPCR_LPTA_EN_MASK));
        __enable_irq();

      datetime_t tm;
      time2date(secs, &tm, 1970);
      encodeTimestamp(startTime,&tm);
    }

   void goDormant(void) 
    {
      powerDown();
    }

  void rtc_init(void) {}
  bool rtc_is_running(void) {return 0;}

  bool rtc_get_datetime(datetime_t *t)
  {
    time2date(rtc_get(), t, 1970);
    return 1;
  }

  bool rtc_set_datetime(const datetime_t *t)
  {
    rtc_set(date2time(t,1970));
    return 1;
  }

  char uid_strng[10]; 
  void getUID(void) { sprintf(uid_strng,"%08lX",(HW_OCOTP_MAC0 & 0xFFFFFFFF)); }
#endif