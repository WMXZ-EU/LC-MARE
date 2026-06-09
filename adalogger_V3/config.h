/* microPAM 
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
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
#ifndef CONFIG_H
#define CONFIG_H
//

  #define START_FLAG 0
  //------------------------------------
  // acquisition constants
  //------------------------------------
  #define T_ACQ     60  // seconds
  #define T_ON      1   // minutes
  #define T_REP     0   // minutes (for continuous recording set t_rep < t_acq)

  // definitions for acquisition and filing (default to highest useful frequency)
  #define FSAMP 96000  // will be limited to 96000 if RP2040
  //
  #define PROC      1   // 0 Wav raw file; 1 compress 

  //for filing meta data
  #define SRC_str "LC17"     //  Source
  #define CMS_str "WMXZ"     //  Organization
  #define ART_str "WMXZ"     // 'Artist'  (creator)
  #define PRD_str "microPAM" // 'Product' (Activity)
  #define SBJ_str "______"   // 'subject' (Area)
  #define NAM_str "Test"     // 'Name'    (location id)


#endif