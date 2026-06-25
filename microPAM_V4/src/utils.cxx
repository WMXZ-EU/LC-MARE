
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
#include <Arduino.h>

void printHex0(uint8_t val,int flag) {if(flag || (val > 0)) Serial.print(val,HEX); else Serial.print(" ");}
void printHex8(uint8_t val,int flag)   { printHex0(val>>4,flag);   flag |= (val>>4)>0;   printHex0(val & 0xF,flag);}
void printHex16(uint16_t val,int flag) { printHex8(val>>8,flag);   flag |= (val>>8)>0;   printHex8(val & 0xFF,flag);}
void printHex32(uint32_t val,int flag) { printHex16(val>>16,flag); flag |= (val>>16)>0;  printHex16(val &0xFFFF,flag);}

void printFloat(float val,int n1)
{ float v0=10.0f;
  for(int ii=1; ii<n1;ii++) v0 =v0*10.0f;
  while(1)
  {
    if((val<v0) && (v0>1.0f)) Serial.print(' '); else break;
    v0 /=10.0f;
    if(v0<0.01) break;
  }
  Serial.print(val);
}
