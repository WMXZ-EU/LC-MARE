#include "Wire.h"

void test_wire(TwoWire *wire)
{
        wire->begin();
        delay(100);
        wire->setClock(100'000);

    for(int ii=0;ii<127;ii++)
    {
        wire->beginTransmission(ii);
        delay(100);
        uint8_t error = wire->endTransmission();
        if(error) 
        {
            //Serial.printf("I2C not found %x\n",ii);  
            continue;
        }
        else 
            Serial.printf("I2C found %x\n",ii);      
    }
    Serial.println("Done");
    //while(1);  
}

void setup() {
  // put your setup code here, to run once:
  while(!Serial); 
      #define ADC_EN      5
      #define ADC_SHDNZ   6

        pinMode(ADC_EN,OUTPUT);
        digitalWrite(ADC_EN,HIGH);
        pinMode(ADC_SHDNZ,OUTPUT);
        digitalWrite(ADC_SHDNZ,LOW);
        delay(100);
        digitalWrite(ADC_SHDNZ,HIGH);

      Serial.println("wire");
      test_wire(&Wire);
}

void loop() {
  // put your main code here, to run repeatedly:

}
