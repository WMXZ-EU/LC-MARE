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
 
#ifndef _MENU_H
#define _MENU_H
  extern int16_t monitor;

  int16_t menu(int16_t status);
  int16_t menu1(int16_t status);
  void menu2(void);
  void menu3(void);

  void saveParameters(void);
  uint16_t *loadParameters(void) ;
  uint16_t *getStore(void) ;

/*
  commands
  s : start aquisition
  e : stop acquisition
  m : toggle monitor
  r : reset usb (to refresh MTP)
  b : reboot CPU
  x : shutdown (stop CPU)

  :w  : store parameters in eeprom
  :c  : xfer time from internal to external rtc

  ?p  : print parameters
  ?d  : get date
  ?t  : get time
  ?a  : get acquisition file length (s)
  ?o  : get acquisition on time (s)
  ?r  : get repeat interval (s); r<o: continuous acquisition
  ?f  : get sampling frequency
  ?s  : get bit shift (right to reduce lower noisy bits) 
  ?c  : get processing mode; 0:raw, 1: integer-compression
  ?g  : get analog gain
  ?1  : get start of first acquisition period (h)
  ?2  : get end of first acquisition period (h)
  ?3  : get start of second acquisition period (h)
  ?4  : get end of second acquisition period (h)
  ?w  : get status of stored parameters 

  !d dd-mm-yyyy\r : set date
  !t HH-MM-SS\r   : set time
  !a xx\r : set acquisition file length (s)
  !o xx\r : set acquisition on time (s)
  !r xx\r : set repeat interval (s); r<o: continuous acquisition
  !f xx\r : set sampling frequency
  !s xx\r : set bit shift (right to reduce lower noisy bits) 
  !c xx\r : set processing mode; 0:raw, 1: integer-compression
  !g xx\r : set analog gain
  !1 xx\r : set start of first acquisition period   t_1 (h) (t_1 >= 0)
  !2 xx\r : set end of first acquisition period     t_2 (h) (t_2 >= t_1)
  !3 xx\r : set start of second acquisition period  t_3 (h) (t_3 >= t_2)
  !4 xx\r : set end of second acquisition period    t_4 (h) (t_4 >= t_3) && (t_4 <= 24)
  !w xx\r : set status for parameters to be stored (0 to wait for input, 1 start acq immediately)
*/
#endif
