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
 
#ifndef I2C_H
#define I2C_H

#include <stdint.h>

    #include <Wire.h>

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
    };


    void test_wire(TwoWire *wire);
#endif