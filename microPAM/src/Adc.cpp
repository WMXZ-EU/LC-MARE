/* microPAM 
 * Copyright (c) 2023/2024, Walter Zimmer
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
#include "Config.h"
#include "Adc.h"
#include "I2C.h"

#if  (ADC_MODEL == TLV320ADC6140)
    #if defined(__IMXRT1062__)
        #define NPORT_I2S    1
        #if 1
            #define ADC_SHDNZ   32
            #define ADC_EN      33      // as of micoPAM-mare-2b
            #define mWire       Wire1    // SDA1/SCL1
        #else                           // as of micoPAM-mare 26-06-2024
            #define ADC_SHDNZ    3
            #define ADC_EN       2      
            #define mWire       Wire    // SDA/SCL0
        #endif
        //
        #define USB_POWER    1
    #elif defined(TARGET_RP2040)
        #define NPORT_I2S 1
        #define ADC_SHDNZ 32
        #undef ADC_EN
        #define USB_POWER 0
        #define mWire       Wire
    #endif

	#if (NCH <= 2)
		//const  uint8_t chanMask[2] = {0b1001<<4, 0b1001<<4};
        //const  uint8_t chmap[2][4] = {{0,2,3,1}, {0,2,3,1}};
//		const  uint8_t chanMask[2] = {0b0011<<4, 0b0011<<4}; 
//        const  uint8_t chmap[2][4] = {{2,3,0,1}, {2,3,0,1}}; 
		const  uint8_t chanMask[2] = {0b1111<<4, 0b1111<<4};
        const  uint8_t chmap[2][4] = {{0,1,2,3}, {0,1,2,3}};
	#else 
		const  uint8_t chanMask[2] = {0b1111<<4, 0b1111<<4};
        const  uint8_t chmap[2][4] = {{0,1,2,3}, {0,1,2,3}};
	#endif
    volatile int16_t again = AGAIN ;              // 0:42
    volatile int16_t dgain = DGAIN;               // (-200:54)/2

/******************************* TLV320ADC6140 ********************************************/
    #define I2C_ADDRESS1 0x4C // 0-0
    #define I2C_ADDRESS2 0x4D // 0-1
    static const uint8_t i2c_addr[2]= {I2C_ADDRESS1, I2C_ADDRESS2};
    static const uint8_t regs[4]={0x3C, 0x41, 0x46, 0x4B};

    void usbPowerInit()
    {
      IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_40 = 5;
      IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_40 = 0x0008; // slow speed, weak 150 ohm drive

      GPIO8_GDIR |= 1<<26;
    }
    void usbPowerOn()
    {
      GPIO8_DR_SET = 1<<26;
    }
    void usbPowerOff()
    {
      GPIO8_DR_CLEAR = 1<<26;
    }

    void usbPowerSetup(void)
    {
      #if USB_POWER==1
        usbPowerInit();
        usbPowerOn();
        delay(1000);
      #endif
    }

    void acqPower(int flag)
    {   
        #if defined(ADC_EN)
            digitalWrite(ADC_EN,flag);
            delay(100);
        #else
            (void) flag;
        #endif
    }

    void adcReset(void) 
    { digitalWrite(ADC_SHDNZ,LOW); 
    }
    void adcStart(void) 
    { digitalWrite(ADC_SHDNZ,HIGH); 
    }


    void adc_init(void)
    {
        #if defined(ADC_EN)
            pinMode(ADC_EN,OUTPUT);
        #endif
        acqPower(HIGH);

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
                {  Serial.printf("ADC I2C %x not found\n",i2c_addr[ii]);/* while(1) ; */}

            i2c.write(i2c_addr[ii],0x02,0x81); // 1.8V AREG, not sleep

            i2c.write(i2c_addr[ii],0x07,(3<<4)); // TDM; 32 bit; default clock xmit on rising edge); zero fill
            i2c.write(i2c_addr[ii],0x08,0x00); // TX_offset 0

            for(int jj=0;jj<4;jj++)
            {
                i2c.write(i2c_addr[ii],0x0B+jj,chmap[ii][jj]); 
            }

            //i2c.write(i2c_addr[ii],0x73,chanMask[ii]); 	//Enable Input Ch-1 to Ch-8 by I2C write into P0_R115 
            i2c.write(i2c_addr[ii],0x73,0x30);	//Enable ASI Output Ch-1 to Ch-8 slots by I2C write into P0_R116
            //i2c.write(i2c_addr[ii],0x74,chanMask[ii]);	//Enable ASI Output Ch-1 to Ch-8 slots by I2C write into P0_R116
            i2c.write(i2c_addr[ii],0x74,0x20);	//Enable ASI Output Ch-1 to Ch-8 slots by I2C write into P0_R116
            i2c.write(i2c_addr[ii],0x75,0xE0);			//Power-up ADC and PLL by I2C write into P0_R117 

            i2c.write(i2c_addr[ii],0x6B,(2<<4) | (1<<2) | (1<<0)); 	//LL-Filter and sum (1+2)/2; (3+4)/2

            i2c.write(i2c_addr[ii],0x3B,0x60);  // 0: 2.75V; 1: 2.5V; 2: 1.375V

            for(int jj=0; jj<4; jj++)
            {   
                i2c.write(i2c_addr[ii],regs[jj]+0, 0x88);  // CH1_CFG0 (Line in, 20 kOhm))
                i2c.write(i2c_addr[ii],regs[jj]+1, again); // CH1_CFG1 (0dB gain)
                i2c.write(i2c_addr[ii],regs[jj]+2, 201+dgain);   // CH1_CFG2
                i2c.write(i2c_addr[ii],regs[jj]+3, 0x80);  // CH1_CFG3 (0dB decimal gain correction: +/- 0.8 dB) 
                i2c.write(i2c_addr[ii],regs[jj]+4, 0x00);  // CH1_CFG4 (0bit)
            }
            Serial.print("0x15: "); Serial.println(i2c.read(i2c_addr[ii],0x15),HEX);
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
    void acqPower(int flag) {(void) flag;}
    void adcReset(void) {}
    void adcStart(void) {}
    void setAGain(int8_t again) {(void) again;}
    void adcStatus(void) {}
    volatile int16_t again = 0 ; 
    volatile int16_t dgain = 0 ; 
#endif
