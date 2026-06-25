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
 
#ifndef FILING_H
#define FILINH_H

extern uint16_t t_acq;   // seconds
extern uint16_t t_on;    // minutes
extern uint16_t t_rep;   // minutes (for continuous recording set t_rep < t_acq)

extern uint16_t h_rec[];

extern char ISRC[]; //  Source
extern char ICMS[]; //  Organization
extern char IART[]; // 'Artist' (creator)
extern char IPRD[]; // 'Product' (Activity)
extern char ISBJ[]; // 'subject' (Area)
extern char INAM[]; // 'Name' (location id)

extern char startTime[]; // Start Time

uint16_t SD_init(void);
void SD_stop(void); 

extern uint32_t diskBuffer[];
extern uint32_t logBuffer[];
status_t logger(status_t status);
void storeConfigToFile(void);
int configShow(void);
#endif