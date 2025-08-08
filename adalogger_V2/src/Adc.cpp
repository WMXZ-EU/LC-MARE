/* microPAM 
 * Copyright (c) 2023/2024/2025, Walter Zimmer
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
#include "Wire.h"
#include "global.h"
#include "Adc.h"
#include "I2C.h"

#if  (ADC_MODEL == TLV320ADC6140)
    #if defined(__IMXRT1062__)
        #define NPORT_I2S    1
        #if 0
            #define HP_ON       -1  
            #define ADC_SHDNZ   32
            #define ADC_EN      33      // as of micoPAM-mare-2b
            #define mWire       Wire1    // SDA1/SCL1
            //
        #else                           // as of micoPAM-mare 26-06-2024 (e.g. LC-MARE)
            #define HP_ON        4      // used for HPAB (does not harm otherwise)
            #define ADC_SHDNZ    3
            #define ADC_EN       2      
            #define mWire       Wire    // SDA0/SCL0
        #endif
        //
        #define USB_POWER    1
    #
    #elif defined(ARDUINO_ARCH_RP2040)
        #define NPORT_I2S   1
        //#define ADC_SHDNZ   32
        //#define USB_POWER   0
        #define ADC_EN      5
        #define ADC_SHDNZ   6
        #define HP_ON       14      
        #define mWire       Wire
    #endif

	#if (NCHAN_I2S ==1)
        #if PreAmp==2
            const  uint8_t chanMask[2] = {0b1000<<4, 0b1000<<4};
            const  uint8_t chmap[2][4] = {{0,1,2,3}, {0,1,2,3}};
        #else
            const  uint8_t chanMask[2] = {0b0100<<4, 0b0100<<4};
            const  uint8_t chmap[2][4] = {{3,0,1,2}, {3,0,1,2}};
        #endif
	#elif (NCHAN_I2S ==2)
		//const  uint8_t chanMask[2] = {0b1010<<4, 0b1010<<4};
        //const  uint8_t chmap[2][4] = {{0,2,1,3}, {0,2,1,3}};
		const  uint8_t chanMask[2] = {0b0110<<4, 0b0110<<4};
        const  uint8_t chmap[2][4] = {{3,0,1,2}, {3,0,1,2}};
	#elif (NCHAN_I2S ==4)
		const  uint8_t chanMask[2] = {0b1111<<4, 0b1111<<4};
        const  uint8_t chmap[2][4] = {{0,1,2,3}, {0,1,2,3}};
	#else 
		const  uint8_t chanMask[2] = {0b1111<<4, 0b1111<<4};
        const  uint8_t chmap[2][4] = {{0,1,2,3}, {0,1,2,3}};
	#endif
  //
  uint32_t again = AGAIN ;                      // 0:42
  volatile int32_t dgain = DGAIN;               // (-200:54)/2

/******************************* TLV320ADC6140 ********************************************/
    #define I2C_ADDRESS1 0x4C // 0-0
    #define I2C_ADDRESS2 0x4D // 0-1
    static const uint8_t i2c_addr[2]= {I2C_ADDRESS1, I2C_ADDRESS2};
    static const uint8_t regs[4]={0x3C, 0x41, 0x46, 0x4B};

    // use usb host 5V power (has 100uF capacitor)
    void usbPowerInit()
    {
//      IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_40 = 5;
//      IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_40 = 0x0008; // slow speed, weak 150 ohm drive
//      GPIO8_GDIR |= 1<<26;
    }
    void usbPowerExit()
    {
//      IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_40 = 0; // disable
//      GPIO8_GDIR &= ~(1<<26);
    }

    void usbPowerOn()  { /*GPIO8_DR_SET = 1<<26;*/ }
    void usbPowerOff() { /*GPIO8_DR_CLEAR = 1<<26;*/ }

    void usbPowerSetup(void)
    {
      #if USB_POWER==1
        usbPowerInit();
        usbPowerOn();
        delay(100);
      #endif
    }

    // enable ADC board LDO
    void acqPower(int flag)
    {   
        #if ADC_EN>0
            digitalWrite(ADC_EN,flag);
            delay(100);
        #else
            (void) flag;
        #endif
    }

    // enable ADC board LDO
    void hpPower(int flag)
    { return;
      #if HP_ON>0
        digitalWrite(HP_ON,flag);
      #endif
    }

    // handle ADC shutdown pin
    void adcReset(void) 
    { digitalWrite(ADC_SHDNZ,LOW);
    }
    void adcStart(void) 
    { digitalWrite(ADC_SHDNZ,HIGH);
    }

    void adc_exit(void)
    {
        // reset ADC's 
        adcReset();
        acqPower(LOW);
        
        hpPower(LOW);
        usbPowerOff();
        usbPowerExit();
    }

    // initialize ADC
    void adc_init(void)
    {
      usbPowerSetup();
        #if defined(ADC_EN) 
          pinMode(ADC_EN,OUTPUT);
          acqPower(HIGH);
        #endif

        // preamp
        #if HP_ON>0
          pinMode(HP_ON,OUTPUT);
          hpPower(HIGH);
        #endif

        // reset ADC's 
        pinMode(ADC_SHDNZ,OUTPUT);
        adcReset();
        delay(100);
        adcStart();

        /* ADDRESS L,L: 0x4C ; H,L: 0x4D; L,H: 0x4E; H,H: 0x4F */
        i2c_class i2c(&mWire,100'000); 

        // check existance of device
        for(int ii=0; ii<NPORT_I2S; ii++)
        {
            if(i2c.exist(i2c_addr[ii]))
                Serial.printf("found %x\n",i2c_addr[ii]);
            else
            {  Serial.printf("ADC I2C %x not found\n",i2c_addr[ii]); continue;}

            i2c.write(i2c_addr[ii],0x02,0x81); // 1.8V AREG, not sleep

            i2c.write(i2c_addr[ii],0x07,(3<<4)); // TDM; 32 bit; default clock xmit on rising edge); zero fill
            i2c.write(i2c_addr[ii],0x08,0x00); // TX_offset 0

            for(int jj=0;jj<4;jj++)
            {
                i2c.write(i2c_addr[ii],0x0B+jj,chmap[ii][jj]); 
            }
            //
            //Enable Input Ch-1 to Ch-8 by I2C write into P0_R115
            //i2c.write(i2c_addr[ii],0x73,0xf0); //0x30
            i2c.write(i2c_addr[ii],0x73,chanMask[ii]); 	 
            //
            //Enable ASI Output Ch-1 to Ch-8 slots by I2C write into P0_R116
            //i2c.write(i2c_addr[ii],0x74,0xf0);	//0x20
            i2c.write(i2c_addr[ii],0x74,chanMask[ii]);	
            //
   			//Power-up ADC and PLL by I2C write into P0_R117 
            i2c.write(i2c_addr[ii],0x75,0xE0);      // 0xE0 = 1<<7: MIC; 1<<6: ADC; 1<<5: PLL

            i2c.write(i2c_addr[ii],0x3B,(6<<4)      // micBias set to 6:AVDD
                                        |(0<<0));   // ADC Full scale (VREF) // 0: 2.75V; 1: 2.5V; 2: 1.375V

            i2c.write(i2c_addr[ii],0x6B,(2<<4)      // 2:ultra low latency
                                        /*| (1<<2)    // sum (1,2) and (3,4)*/ 
                                        | (1<<0));  //0.00025*fs HP filter

            for(int jj=0; jj<4; jj++)
            {   
                i2c.write(i2c_addr[ii],regs[jj]+0, 0x88);  // CH1_CFG0 (Line in, 20 kOhm))
                i2c.write(i2c_addr[ii],regs[jj]+1, again<<2); // CH1_CFG1 (0dB gain)
                i2c.write(i2c_addr[ii],regs[jj]+2, 201+dgain);   // CH1_CFG2
                i2c.write(i2c_addr[ii],regs[jj]+3, 0x80);  // CH1_CFG3 (0dB decimal gain correction: +/- 0.8 dB) e
                i2c.write(i2c_addr[ii],regs[jj]+4, 0x00);  // CH1_CFG4 (0bit)
            }
            delay(10); // give time to settle before reading back (0x76 sometimes reads 0x00)
            Serial.print("0x15: "); Serial.println(i2c.read(i2c_addr[ii],0x15),HEX);
            Serial.print("0x73: "); Serial.println(i2c.read(i2c_addr[ii],0x73),HEX);
            Serial.print("0x74: "); Serial.println(i2c.read(i2c_addr[ii],0x74),HEX);
            Serial.print("0x76: "); Serial.println(i2c.read(i2c_addr[ii],0x76),HEX);
            
        }
    }

    void setAGain(int8_t again)
    {
        i2c_class i2c(&mWire,100'000);
        for(int ii=0; ii<NPORT_I2S; ii++)
            for(int jj=0; jj<4; jj++)
            {
                i2c.write(i2c_addr[ii],regs[jj]+1, again); // CH1_CFG1 (0dB gain)
            }
    }
    void adcStatus(void)
    {
        i2c_class i2c(&mWire,100'000);
        for(int ii=0; ii<NPORT_I2S; ii++)
        {   Serial.print("\n0x15: "); Serial.print(i2c.read(i2c_addr[ii],0x15),HEX);
            Serial.print("\n0x76: "); Serial.print(i2c.read(i2c_addr[ii],0x76),HEX);
        }
        Serial.println();
    }
#else
    // there is no ADC to be controlled
    void adc_init(void) {}
    void adc_exit(void) {}
    void usbPowerSetup(void){}
    void acqPower(int flag) {(void) flag;}
    void adcReset(void) {}
    void adcStart(void) {}
    void setAGain(int8_t again) {(void) again;}
    void adcStatus(void) {}
    volatile int16_t again = 0 ; 
    volatile int16_t dgain = 0 ; 
#endif
