#include "global.h"
#include "Wire.h"
#include "acq.h"

  #if MCU==T_4_1
    #include "Teensy.h"
  #else
    #include "rp2x.h"
  #endif

    class i2c_class
    {   TwoWire *wire;

        public:
        i2c_class(TwoWire *wire) ;
        i2c_class(TwoWire *wire, uint32_t speed) ;
        i2c_class(TwoWire *wire, uint32_t speed, uint8_t scl, uint8_t sda) ;
        uint8_t exist(uint8_t addr);
        uint8_t read(uint8_t addr, uint8_t reg) ;
        uint8_t write(uint8_t addr, uint8_t reg) ;
        uint8_t write(uint8_t addr, uint8_t reg, uint8_t val) ;
        uint8_t *readData( uint8_t addr, uint8_t reg, uint8_t *data, uint8_t ndat, uint16_t dt = 0);
        uint8_t writeData( uint8_t addr, uint8_t reg, uint8_t *data, uint8_t ndat);
    };

    i2c_class::i2c_class(TwoWire *wire) 
    {   this->wire = wire;
        wire->begin();
        delay(100);
    }

    i2c_class::i2c_class(TwoWire *wire, uint32_t speed) 
    {   this->wire = wire;
        wire->begin();
        delay(100);
        wire->setClock(speed);
    }

    i2c_class::i2c_class(TwoWire *wire, uint32_t speed, uint8_t scl, uint8_t sda) 
    {   this->wire = wire;
        wire->begin();
        delay(100);
        wire->setClock(speed);
        wire->setSCL(scl);
        wire->setSDA(sda);
    }

    uint8_t i2c_class::exist(uint8_t addr)
    {
        wire->beginTransmission(addr);
        return (wire->endTransmission()==0);
    }

    uint8_t i2c_class::read(uint8_t addr, uint8_t reg) 
    { 
        unsigned int val;
        wire->beginTransmission(addr);
        wire->write(reg);
        if (wire->endTransmission(false) != 0) return 0;
        if (wire->requestFrom((int)addr, 1) < 1) return 0;
        val = wire->read();
        return val;
    }
    
    uint8_t i2c_class::write(uint8_t addr, uint8_t reg) 
    { 
        wire->beginTransmission(addr);
        wire->write(reg);
        return (wire->endTransmission() == 0) ;
    }

    uint8_t i2c_class::write(uint8_t addr, uint8_t reg, uint8_t val) 
    { 
        wire->beginTransmission(addr);
        wire->write(reg);
        wire->write(val);
        return (wire->endTransmission() == 0) ;
    }

    uint8_t *i2c_class::readData( uint8_t addr, uint8_t reg, uint8_t *data, uint8_t ndat, uint16_t dt)
    {
            wire->beginTransmission(addr);
            wire->write(reg);
            if (wire->endTransmission(false) != 0) return 0;
            if (dt>0) delay(dt);
            if (wire->requestFrom((int)addr, (int)ndat) < 1) return 0;
            for(int ii=0;ii<ndat;ii++) data[ii] = wire->read();
            return data;
    }    

    uint8_t i2c_class::writeData( uint8_t addr, uint8_t reg, uint8_t *data, uint8_t ndat)
    {
            wire->beginTransmission(addr);
            wire->write(reg);
            for(int ii=0;ii<ndat;ii++) wire->write(data[ii]);
            return (wire->endTransmission() == 0);
    }    

  /*--------------------------- TLV320ADC6140 ----------------------------------------------*/
    //
    uint32_t again = AGAIN ;                      // 0:42
    volatile int32_t dgain = DGAIN;               // (-200:54)/2

		const  uint8_t chanMask[2] = {0b0110<<4, 0b0110<<4};
    const  uint8_t chmap[2][4] = {{3,0,1,2}, {3,0,1,2}};

    #define I2C_ADDRESS1 0x4C // 0-0
    #define I2C_ADDRESS2 0x4D // 0-1
    static const uint8_t i2c_addr[2]= {I2C_ADDRESS1, I2C_ADDRESS2};
    static const uint8_t regs[4]={0x3C, 0x41, 0x46, 0x4B};

    // enable ADC board LDO
    void acqPower(int flag)
    { digitalWrite(ADC_EN,flag);
      delay(100);
    }

    // handle ADC shutdown pin
    void adcReset(void) 
    { digitalWrite(ADC_SHDNZ,LOW);
    }
    void adcStart(void) 
    { digitalWrite(ADC_SHDNZ,HIGH);
    }

    void adc_exit(void)
    {   // reset ADC's 
        adcReset();
        acqPower(LOW);
        
        usbPowerOff();
        usbPowerExit();
    }

    // initialize ADC
    void adc_init(void)
    {   usbPowerSetup();
        pinMode(ADC_EN,OUTPUT);
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
            {  Serial.printf("ADC I2C %x not found\n",i2c_addr[ii]); continue;
            }

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
            delay(20); // give time to settle before reading back (0x76 sometimes reads 0x00)
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
