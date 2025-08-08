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
#include <Wire.h>
#include "mRTC.h"

#if USE_EXT_RTC > 0
  #define mWire Wire

  static int16_t i2c_setup(uint8_t sda, uint8_t scl)
  {
    mWire.setSDA(sda);
    mWire.setSCL(scl);
    mWire.begin();
    delay(100);
    return 1;
  }

  static int16_t i2c_exists(uint8_t address)
  {
    mWire.beginTransmission(address);
    return !mWire.endTransmission();
  }

  static void i2c_write_register(uint8_t address, uint8_t reg, uint8_t val) 
  {
    mWire.beginTransmission(address);
    mWire.write(reg);
    mWire.write(val);
    mWire.endTransmission();
  }

  static uint8_t i2c_read_register(uint8_t address, uint8_t reg) 
  {
    mWire.beginTransmission(address);
    mWire.write(reg);
    mWire.endTransmission();

  //  uint8_t buffer[1];
    mWire.requestFrom(address, (uint8_t)1);
    return mWire.read();
  }

  static void i2c_write_data(uint8_t address, uint8_t reg, uint8_t *buffer, uint16_t nbuf)
  {
    mWire.beginTransmission(address);
    mWire.write(reg);
    for(int ii=0; ii<nbuf; ii++) mWire.write(buffer[ii]);
    mWire.endTransmission();  
  }

  static void i2c_read_data(uint8_t address, uint8_t reg, uint8_t *buffer, uint16_t nbuf)
  {
    mWire.beginTransmission(address);
    mWire.write(reg);
    mWire.endTransmission();

    mWire.requestFrom(address, (uint8_t)nbuf);
    for(int ii=0;ii<nbuf;ii++) buffer[ii]=mWire.read();
  }

#endif

  /**************************************************************************************/
  static uint8_t bin2bcd(uint8_t val) { return val + 6 * (val / 10); }
  static uint8_t bcd2bin(uint8_t val) { return val - 6 * (val >> 4); }


#if XRTC==DS3231

  #define DS3231_ADDRESS   0x68   ///< I2C address for DS3231
  #define DS3231_TIME      0x00   ///< Time register
  #define DS3231_ALARM1    0x07   ///< Alarm 1 register
  #define DS3231_ALARM2    0x0B   ///< Alarm 2 register
  #define DS3231_CONTROL   0x0E   ///< Control register
  #define DS3231_STATUSREG 0x0F   ///< Status register
  #define DS3231_TEMPERATUREREG 0x11 

  static uint8_t address = DS3231_ADDRESS;
  static uint8_t status_reg = DS3231_STATUSREG;
  static uint8_t time_reg = DS3231_TIME;
  static uint8_t control_reg = DS3231_CONTROL;
  static uint8_t alarm1_reg = DS3231_ALARM1;
  static uint8_t alarm2_reg = DS3231_ALARM2;

  int16_t initXRTC(uint8_t sda, uint8_t scl)
  {
    i2c_setup(sda,scl);
    return i2c_exists(address);
  }

  int16_t lostPowerXRTC(void) 
  {
    return i2c_read_register(address,status_reg) >> 7;
  }

  uint8_t *getXRTC(uint8_t *buffer,uint16_t nbuf)
  { // read time from RTC
    i2c_read_data(address,time_reg,buffer,nbuf);
    for(int ii=0;ii<nbuf;ii++) buffer[ii]=bcd2bin(buffer[ii]);
    
    return buffer;
  }

  uint8_t *setXRTC(uint8_t *buffer, uint16_t nbuf)
  { // write time to RTC
    for(int ii=0;ii<nbuf;ii++) buffer[ii]=bin2bcd(buffer[ii]);
    i2c_write_data(address,time_reg,buffer,nbuf);
    
    uint8_t statreg = i2c_read_register(address,status_reg);
    //Serial.println(statreg,BIN);
    statreg &= ~0x80; // flip OSF bit
    i2c_write_register(address,status_reg, statreg);

    return buffer;
  }

  void XRTCclearAlarm(void)
  {
    // clear alarm1
    uint8_t regval=i2c_read_register(address,status_reg);
    regval = regval & 0b11111110;
    i2c_write_register(address,status_reg,regval);
  }

  void XRTCsetAlarm(uint32_t secs)
  { //
    uint8_t buffer[7]={0};
    uint16_t nbuf;

    //
    // turn off alarm
    uint8_t regval=i2c_read_register(address,control_reg);
    regval = regval & 0b11111110;
    i2c_write_register(address,control_reg, regval);

    //
    // write alarm time to rtc
    secs=secs % (24*3600); // only seconds in day

    Serial.println(secs);
    //Serial.print(secso % (24*3600)); Serial.print(' -> ');
    //Serial.println(secs % (24*3600));

    int hh,mm,ss;
    buffer[4] = 3+(secs /(24*3600)) % 7;    // dow 1=sunday
    buffer[3] = buffer[4] | (1<<6);         // use dow
    buffer[3] = 1<<7;                       // use only hour,min,sec
    buffer[2] = secs/3600;
    buffer[1] = (secs % 3600)/60;
    buffer[0] = (secs % 60);
    
    Serial.print("Alarm : ");
    Serial.print(buffer[4]); Serial.print(' ');
    Serial.print(buffer[3]&(1<<6-1)); Serial.print(' ');
    Serial.print(buffer[2]); Serial.print(' ');
    Serial.print(buffer[1]); Serial.print(' ');
    Serial.print(buffer[0]); Serial.println(' ');
    Serial.flush();

    for(int ii=0;ii<4;ii++) buffer[ii]=bin2bcd(buffer[ii]);
    i2c_write_data(address,alarm1_reg,buffer,4);

    // clear alarm1
    regval=i2c_read_register(address,status_reg);
    regval = regval & 0b11111110;
    i2c_write_register(address,status_reg,regval);

    // turn on alarm
    regval=i2c_read_register(address,control_reg);
    regval = regval | 0b00000101;
    i2c_write_register(address,control_reg, regval);
  }

  float XRTCgetTemperature(void) {
    uint8_t buffer[2] = {DS3231_TEMPERATUREREG, 0};
    i2c_read_data(address,DS3231_TEMPERATUREREG,buffer,2);
    //
    return (float)buffer[0] + (buffer[1] >> 6) * 0.25f;
  }

#elif XRTC==RV3028
// code partially based on https://github.com/constiko/RV-3028_C7-Arduino_Library

// The 7-bit I2C ADDRESS of the RV3028
  #define RV3028_ADDR						(uint8_t)0x52

  #define TIME_ARRAY_LENGTH 7 // Total number of writable values in device
	static uint8_t _time[TIME_ARRAY_LENGTH];
  enum time_order {
    TIME_SECONDS,    // 0
    TIME_MINUTES,    // 1
    TIME_HOURS,      // 2
    TIME_WEEKDAY,    // 3
    TIME_DATE,       // 4
    TIME_MONTH,      // 5
    TIME_YEAR,       // 6
  };
  #define DECtoBCD bin2bcd
  #define BCDtoDEC bcd2bin
  
  static uint8_t address;

  #define RV3028_SECONDS      		0x00
  #define RV3028_UNIX_TIME0				0x1B

  #define time_reg RV3028_SECONDS

//Configuration registers
#define RV3028_STATUS					0x0E
#define RV3028_CTRL1					0x0F
#define RV3028_CTRL2					0x10
#define RV3028_GPBITS					0x11
#define RV3028_INT_MASK					0x12

#define CTRL1_EERD		3
#define CTRL1_WADA		5//Bit 6 not implemented

#define CTRL2_AIE		3

#define EEPROM_Backup_Register			0x37

#define EEPROMBackup_FEDE_BIT			4				//Fast Edge Detection Enable Bit (for Backup Switchover 
#define EEPROMBackup_BSM_SHIFT			2				//Backup Switchover Mode shift
#define EEPROMBackup_BSM_CLEAR			0b11110011		//Backup Switchover Mode clear

//EEPROM Memory Control registers
#define RV3028_EEPROM_ADDR				0x25
#define RV3028_EEPROM_DATA				0x26
#define RV3028_EEPROM_CMD				0x27

#define STATUS_AF		2
#define STATUS_EEBUSY	7

//Commands for EEPROM Command Register (0x27)
#define EEPROMCMD_First					0x00
#define EEPROMCMD_Update				0x11
#define EEPROMCMD_Refresh				0x12
#define EEPROMCMD_WriteSingle			0x21
#define EEPROMCMD_ReadSingle			0x22

//Bits in Alarm registers
#define MINUTESALM_AE_M		7
#define HOURSALM_AE_H		7
#define DATE_AE_WD			7

//Alarm registers
#define RV3028_MINUTES_ALM     			0x07
#define RV3028_HOURS_ALM       			0x08
#define RV3028_DATE_ALM        			0x09

#define IMT_MASK_CAIE					2				//Clock output when Alarm Interrupt bit.

static uint8_t readRegister(uint8_t addr)
{
  return i2c_read_register(address,addr);
}

static bool writeRegister(uint8_t addr, uint8_t val)
{
  i2c_write_register(address,addr,val);
	return(true);
}

static bool readMultipleRegisters(uint8_t addr, uint8_t * dest, uint8_t len)
{
  i2c_read_data(address,addr,dest,len);
	return(true);
}

static bool writeMultipleRegisters(uint8_t addr, uint8_t * values, uint8_t len)
{
  i2c_write_data(address,addr,values,len);
	return(true);
}

static void setBit(uint8_t reg_addr, uint8_t bit_num)
{
	uint8_t value = readRegister(reg_addr);
	value |= (1 << bit_num); //Set the bit
	writeRegister(reg_addr, value);
}

static void clearBit(uint8_t reg_addr, uint8_t bit_num)
{
	uint8_t value = readRegister(reg_addr);
	value &= ~(1 << bit_num); //Clear the bit
	writeRegister(reg_addr, value);
}

static bool readBit(uint8_t reg_addr, uint8_t bit_num)
{
	uint8_t value = readRegister(reg_addr);
	value &= (1 << bit_num);
	return value;

}

//True if success, false if timeout occured
static bool waitforEEPROM()
{
	unsigned long timeout = millis() + 500;
	while ((readRegister(RV3028_STATUS) & 1 << STATUS_EEBUSY) && millis() < timeout);

	return millis() < timeout;
}


static bool writeConfigEEPROM_RAMmirror(uint8_t eepromaddr, uint8_t val)
{
	bool success = waitforEEPROM();

	//Disable auto refresh by writing 1 to EERD control bit in CTRL1 register
	uint8_t ctrl1 = readRegister(RV3028_CTRL1);
	ctrl1 |= 1 << CTRL1_EERD;
	if (!writeRegister(RV3028_CTRL1, ctrl1)) success = false;
	//Write Configuration RAM Register
	writeRegister(eepromaddr, val);
	//Update EEPROM (All Configuration RAM -> EEPROM)
	writeRegister(RV3028_EEPROM_CMD, EEPROMCMD_First);
	writeRegister(RV3028_EEPROM_CMD, EEPROMCMD_Update);
	if (!waitforEEPROM()) success = false;
	//Reenable auto refresh by writing 0 to EERD control bit in CTRL1 register
	ctrl1 = readRegister(RV3028_CTRL1);
	if (ctrl1 == 0x00)success = false;
	ctrl1 &= ~(1 << CTRL1_EERD);
	writeRegister(RV3028_CTRL1, ctrl1);
	if (!waitforEEPROM()) success = false;

	return success;
}

static uint8_t readConfigEEPROM_RAMmirror(uint8_t eepromaddr)
{
	bool success = waitforEEPROM();

	//Disable auto refresh by writing 1 to EERD control bit in CTRL1 register
	uint8_t ctrl1 = readRegister(RV3028_CTRL1);
	ctrl1 |= 1 << CTRL1_EERD;
	if (!writeRegister(RV3028_CTRL1, ctrl1)) success = false;
	//Read EEPROM Register
	writeRegister(RV3028_EEPROM_ADDR, eepromaddr);
	writeRegister(RV3028_EEPROM_CMD, EEPROMCMD_First);
	writeRegister(RV3028_EEPROM_CMD, EEPROMCMD_ReadSingle);
	if (!waitforEEPROM()) success = false;
	uint8_t eepromdata = readRegister(RV3028_EEPROM_DATA);
	if (!waitforEEPROM()) success = false;
	//Reenable auto refresh by writing 0 to EERD control bit in CTRL1 register
	ctrl1 = readRegister(RV3028_CTRL1);
	if (ctrl1 == 0x00)success = false;
	ctrl1 &= ~(1 << CTRL1_EERD);
	writeRegister(RV3028_CTRL1, ctrl1);

	if (!success) return 0xFF;
	return eepromdata;
}

/*********************************
0 = Switchover disabled
1 = Direct Switching Mode
2 = Standby Mode
3 = Level Switching Mode
*********************************/
static bool setBackupSwitchoverMode(uint8_t val)
{
	if (val > 3)return false;
	bool success = true;

	//Read EEPROM Backup Register (0x37)
	uint8_t EEPROMBackup = readConfigEEPROM_RAMmirror(EEPROM_Backup_Register);
	if (EEPROMBackup == 0xFF) success = false;
	//Ensure FEDE Bit is set to 1
	EEPROMBackup |= 1 << EEPROMBackup_FEDE_BIT;
	//Set BSM Bits (Backup Switchover Mode)
	EEPROMBackup &= EEPROMBackup_BSM_CLEAR;		//Clear BSM Bits of EEPROM Backup Register
	EEPROMBackup |= val << EEPROMBackup_BSM_SHIFT;	//Shift values into EEPROM Backup Register
	//Write EEPROM Backup Register
	if (!writeConfigEEPROM_RAMmirror(EEPROM_Backup_Register, EEPROMBackup)) success = false;

	return success;
}
/*
bool RV3028_setTime(uint8_t sec, uint8_t min, uint8_t hour, uint8_t weekday, uint8_t date, uint8_t month, uint16_t year)
{
	_time[TIME_SECONDS] = DECtoBCD(sec);
	_time[TIME_MINUTES] = DECtoBCD(min);
	_time[TIME_HOURS] = DECtoBCD(hour);
	_time[TIME_WEEKDAY] = DECtoBCD(weekday);
	_time[TIME_DATE] = DECtoBCD(date);
	_time[TIME_MONTH] = DECtoBCD(month);
	_time[TIME_YEAR] = DECtoBCD(year - 2000);

	return RV3028_setTime(_time, TIME_ARRAY_LENGTH);
}

// setTime -- Set time and date/day registers of RV3028 (using data array)
bool RV3028_setTime(uint8_t * time, uint8_t len)
{
	if (len != TIME_ARRAY_LENGTH)
		return false;

	return writeMultipleRegisters(time_reg, time, len);
}

//ATTENTION: Real Time and UNIX Time are INDEPENDENT!
bool RV3028_setUNIX(uint32_t value)
{
	uint8_t unix_reg[4];
	unix_reg[0] = value;
	unix_reg[1] = value >> 8;
	unix_reg[2] = value >> 16;
	unix_reg[3] = value >> 24;

	return writeMultipleRegisters(RV3028_UNIX_TIME0, unix_reg, 4);
}

//ATTENTION: Real Time and UNIX Time are INDEPENDENT!
uint32_t RV3028_getUNIX()
{
	uint8_t unix_reg[4];
	readMultipleRegisters(RV3028_UNIX_TIME0, unix_reg, 4);
	return ((uint32_t)unix_reg[3] << 24) | ((uint32_t)unix_reg[2] << 16) | ((uint32_t)unix_reg[1] << 8) | unix_reg[0];
}
*/


  int16_t initXRTC(uint8_t sda, uint8_t scl)
  { address=RV3028_ADDR;
    i2c_setup(sda,scl);
    Serial.print("XRTC: 0x"); Serial.println(address,HEX);
    delay(100);
    if(i2c_exists(address))
    {
        //Backup Switchover Mode
      Serial.print("Config EEPROM 0x37 before: ");
      Serial.println(readConfigEEPROM_RAMmirror(0x37));

      //setBackupSwitchoverMode(0);   //Switchover disabled
      setBackupSwitchoverMode(1); //Direct Switching Mode
      //setBackupSwitchoverMode(2); //Standby Mode
      //setBackupSwitchoverMode(3); //Level Switching Mode (default)

      Serial.print("Config EEPROM 0x37 after: ");
      Serial.println(readConfigEEPROM_RAMmirror(0x37));

    }
    return true;
  }


float XRTCgetTemperature(void) {return 0.0f;}

  uint8_t *getXRTC(uint8_t *buffer,uint16_t nbuf)
  { // read time from RTC
    i2c_read_data(address,time_reg,buffer,nbuf);
    for(int ii=0;ii<nbuf;ii++) buffer[ii]=bcd2bin(buffer[ii]);
    
    return buffer;
  }

  uint8_t *setXRTC(uint8_t *buffer, uint16_t nbuf)
  { // write time to RTC
    for(int ii=0;ii<nbuf;ii++) buffer[ii]=bin2bcd(buffer[ii]);
    i2c_write_data(address,time_reg,buffer,nbuf);
    return buffer;
  }


//Only disables the interrupt (not the alarm flag)
void disableAlarmInterrupt()
{
	clearBit(RV3028_CTRL2, CTRL2_AIE);
}

bool readAlarmInterruptFlag()
{
	return readBit(RV3028_STATUS, STATUS_AF);
}

void clearAlarmInterruptFlag()
{
	clearBit(RV3028_STATUS, STATUS_AF);
}

void enableAlarmInterrupt()
{
	setBit(RV3028_CTRL2, CTRL2_AIE);
}

/*********************************
  Set the alarm mode in the following way:
  0: When minutes, hours and weekday/date match (once per weekday/date)
  1: When hours and weekday/date match (once per weekday/date)
  2: When minutes and weekday/date match (once per hour per weekday/date)
  3: When weekday/date match (once per weekday/date)
  4: When hours and minutes match (once per day)
  5: When hours match (once per day)
  6: When minutes match (once per hour)
  7: All disabled – Default value
  If you want to set a weekday alarm (alm_isweekday = true), set 'alm_date_or_weekday' from 0 (Sunday) to 6 (Saturday)
********************************/
static void enableAlarmInterrupt(uint8_t min, uint8_t hour, uint8_t date_or_weekday, 
                                bool setWeekdayAlarm_not_Date, uint8_t mode, bool enable_clock_output)
{
	//disable Alarm Interrupt to prevent accidental interrupts during configuration
	disableAlarmInterrupt();
	clearAlarmInterruptFlag();

	//ENHANCEMENT: Add Alarm in 12 hour mode
//	set24Hour();

	//Set WADA bit (Weekday/Date Alarm)
	if (setWeekdayAlarm_not_Date)
		clearBit(RV3028_CTRL1, CTRL1_WADA);
	else
		setBit(RV3028_CTRL1, CTRL1_WADA);

	//Write alarm settings in registers 0x07 to 0x09
	uint8_t alarmTime[3];
	alarmTime[0] = DECtoBCD(min);				//minutes
	alarmTime[1] = DECtoBCD(hour);				//hours
	alarmTime[2] = DECtoBCD(date_or_weekday);	//date or weekday
  // trigger once per day (hour and minutes match)
	alarmTime[2] |= 1 << 7;
	writeMultipleRegisters(RV3028_MINUTES_ALM, alarmTime, 3);

	//enable Alarm Interrupt
	enableAlarmInterrupt();

	//Clock output?
	if (enable_clock_output)
		setBit(RV3028_INT_MASK, IMT_MASK_CAIE);
	else
		clearBit(RV3028_INT_MASK, IMT_MASK_CAIE);
}

void XRTCclearAlarm(void)
{
	disableAlarmInterrupt();
	clearAlarmInterruptFlag();  
}

void XRTCsetAlarm(uint32_t secs)
{ uint8_t min,hour,date;
  datetime_t tm;
  time2date(secs, &tm, 2000);
  Serial.printf("%d %d %d %d %d\n",tm.year,tm.month,tm.day,tm.hour,tm.min);
  enableAlarmInterrupt(tm.min, tm.hour, tm.day, false, 0, false); 
} 


// alm_mode=6
// alm_minute=
// alm_date_or_weekdaye=
// alm_isweekday=false
//enableAlarmInterrupt(alm_minute, alm_hour, alm_date_or_weekday, alm_isweekday, alm_mode);


/*
  #define DS3231_ADDRESS   0x68   ///< I2C address for DS3231
  #define DS3231_TIME      0x00   ///< Time register
  #define DS3231_ALARM1    0x07   ///< Alarm 1 register
  #define DS3231_ALARM2    0x0B   ///< Alarm 2 register
  #define DS3231_CONTROL   0x0E   ///< Control register
  #define DS3231_STATUSREG 0x0F   ///< Status register
  #define DS3231_TEMPERATUREREG 0x11 

  static uint8_t address = DS3231_ADDRESS;
  static uint8_t status_reg = DS3231_STATUSREG;
  static uint8_t time_reg = DS3231_TIME;
  static uint8_t control_reg = DS3231_CONTROL;
  static uint8_t alarm1_reg = DS3231_ALARM1;
  static uint8_t alarm2_reg = DS3231_ALARM2;

  int16_t lostPowerXRTC(void) 
  {
    return i2c_read_register(address,status_reg) >> 7;
  }

  uint8_t *setXRTC(uint8_t *buffer, uint16_t nbuf)
  { // write time to RTC
    for(int ii=0;ii<nbuf;ii++) buffer[ii]=bin2bcd(buffer[ii]);
    i2c_write_data(address,time_reg,buffer,nbuf);
    
    uint8_t statreg = i2c_read_register(address,status_reg);
    //Serial.println(statreg,BIN);
    statreg &= ~0x80; // flip OSF bit
    i2c_write_register(address,status_reg, statreg);

    return buffer;
  }

  void XRTCclearAlarm(void)
  {
    // clear alarm1
    uint8_t regval=i2c_read_register(address,status_reg);
    regval = regval & 0b11111110;
    i2c_write_register(address,status_reg,regval);
  }

  void XRTCsetAlarm(uint32_t secs)
  { //
    uint8_t buffer[7]={0};
    uint16_t nbuf;

    //
    // turn off alarm
    uint8_t regval=i2c_read_register(address,control_reg);
    regval = regval & 0b11111110;
    i2c_write_register(address,control_reg, regval);

    //
    // write alarm time to rtc
    secs=secs % (24*3600); // only seconds in day

    Serial.println(secs);
    //Serial.print(secso % (24*3600)); Serial.print(' -> ');
    //Serial.println(secs % (24*3600));

    int hh,mm,ss;
    buffer[4] = 3+(secs /(24*3600)) % 7;    // dow 1=sunday
    buffer[3] = buffer[4] | (1<<6);         // use dow
    buffer[3] = 1<<7;                       // use only hour,min,sec
    buffer[2] = secs/3600;
    buffer[1] = (secs % 3600)/60;
    buffer[0] = (secs % 60);
    
    Serial.print("Alarm : ");
    Serial.print(buffer[4]); Serial.print(' ');
    Serial.print(buffer[3]&(1<<6-1)); Serial.print(' ');
    Serial.print(buffer[2]); Serial.print(' ');
    Serial.print(buffer[1]); Serial.print(' ');
    Serial.print(buffer[0]); Serial.println(' ');
    Serial.flush();

    for(int ii=0;ii<4;ii++) buffer[ii]=bin2bcd(buffer[ii]);
    i2c_write_data(address,alarm1_reg,buffer,4);

    // clear alarm1
    regval=i2c_read_register(address,status_reg);
    regval = regval & 0b11111110;
    i2c_write_register(address,status_reg,regval);

    // turn on alarm
    regval=i2c_read_register(address,control_reg);
    regval = regval | 0b00000101;
    i2c_write_register(address,control_reg, regval);
  }

  float XRTCgetTemperature(void) {
    uint8_t buffer[2] = {DS3231_TEMPERATUREREG, 0};
    i2c_read_data(address,DS3231_TEMPERATUREREG,buffer,2);
    //
    return (float)buffer[0] + (buffer[1] >> 6) * 0.25f;
  }
*/
#endif

  #define T2000 946684800 // seconds different between 1-jan-1970 and 1-jan-2000
  #define D2000 10957     // day different between 1-jan-1970 and 1-jan-2000

  #define LEAP_YEAR(Y)  (((Y % 4) == 0) && (((Y % 100) != 0) || ((Y % 400) == 0)))

  const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  void time2date(uint32_t seconds, datetime_t *tm, uint16_t epoch)
  {
    uint32_t days, year, month, dotw;

    uint32_t time = seconds;
   //Retrieve hours, minutes and seconds
    tm->sec =  time % 60;  time /= 60;
    tm->min  = time % 60;  time /= 60;
    tm->hour = time % 24;  time /= 24;

    // time counts now days since epoch (e.g. 1970)
    if(epoch==1970)  dotw = 4; else if(epoch==2000)  dotw = (D2000+4)%7;
    tm->dotw = (time+dotw)%7;    //Unix time starts in 1970 on a Thursday (4)

    year = epoch;
    days = 0;
    while((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) year++;
    tm->year = year;

    days -= LEAP_YEAR(year) ? 366 : 365;
    days = time - days; // now time is days in this year, starting at 0
    Serial.print(days); Serial.print(" ");

    month=0;
    uint16_t monthLength=0;
    for (month=1; month<12; month++) 
    {
      monthLength = daysInMonth[month-1];  
      if ((month==2) & LEAP_YEAR(year)) monthLength++; // february
      
      if (days< monthLength) break;
      days -= monthLength;
    }
    tm->day    = days+1;  // days started from 0
    tm->month  = month;   // is already incremented
  }

  uint32_t date2time(datetime_t *tm, uint16_t epoch)
  {
    uint32_t seconds;

    uint16_t year;
    year=tm->year; 
    uint32_t days= 0;
    for (int y = epoch; y < year; y++)  days += (LEAP_YEAR(y)? 366: 365);

    // add days for this year, months start from 1
    for (int ii = 1; ii < tm->month; ii++) 
    {
      days +=  daysInMonth[ii-1];  
      if ( (ii == 2) && LEAP_YEAR(year)) days++;
    }
    seconds= days*86400;
    seconds+= (tm->day -1) * 86400;
    seconds+= tm->hour * 3600;
    seconds+= tm->min * 60;
    seconds+= tm->sec;
    return seconds; 
  }


  static volatile uint16_t haveExtRTC=0;

  void XRTCgetDatetime(datetime_t *t)
  {
    uint8_t rtcBuffer[7] = {0};//{0,27,15,1,3,4,23}; // adapt to better time (secs,min, ...., year)
    if(haveExtRTC)
    {
      getXRTC(rtcBuffer,7);
      t->year =rtcBuffer[6]+2000; //XRTC reference from y2000
      t->month=rtcBuffer[5]&0x7f;
      t->day  =rtcBuffer[4];

      t->hour=rtcBuffer[2];
      t->min =rtcBuffer[1];
      t->sec =rtcBuffer[0] &0xff;
    }
  }
  void XRTCsetDatetime(datetime_t *t)
  {
    uint8_t rtcBuffer[7] = {0};//{0,27,15,1,3,4,23}; // adapt to better time (secs,min, ...., year)

    if(haveExtRTC)
    {     
      rtcBuffer[6]=t->year-2000;  //XRTC reference from y2000
      rtcBuffer[5]=t->month;
      rtcBuffer[4]=t->day;
      rtcBuffer[2]=t->hour;
      rtcBuffer[1]=t->min;
      rtcBuffer[0]=t->sec;
      for(int ii=0;ii<7;ii++) {Serial.print(rtcBuffer[ii]); Serial.print(' ');} Serial.println();
      setXRTC(rtcBuffer,7);   // write to ExtRTC
    }
  }

  void printDatetime(const char *str, datetime_t *t)
  {
    Serial.printf("%s %04d-%02d-%02d ",str, t->year,t->month,t->day);
    Serial.printf("%02d:%02d:%02d\n",t->hour,t->min,t->sec);
  }

/**************************************************************************************/  
#if defined(ARDUINO_ADAFRUIT_FEATHER_RP2040_ADALOGGER)

  uint32_t rtc_get(void)
  { // get seconds since epoch time
    datetime_t tm;
    // get local time
    rtc_get_datetime(&tm);    
    return date2time(&tm, 2000);
  }

  void rtc_set(uint32_t tt)
  {
    datetime_t tm;
    time2date(tt,&tm,2000);
    // set local time
    rtc_set_datetime(&tm);
  }

#else
  void rtc_init(void) {}

  bool rtc_get_datetime(datetime_t *t)
  {
    time2date(rtc_get(), t);
    return 1;
  }

  bool rtc_set_datetime(datetime_t *t)
  {
    rtc_set(date2time(t));
    return 1;
  }

#endif

  int16_t rtc_setup(void)
  {
    rtc_init(); // hardware (MCU) rtc
    haveExtRTC=initXRTC(RTC_SDA, RTC_SCL); // external rtc clock (XRTC==NONE return 0)
    Serial.print("extClock "); Serial.println(haveExtRTC);
    delay(10);

    // get time from ext RTC
    if(haveExtRTC)
    {
      datetime_t t;
      XRTCgetDatetime(&t);
      printDatetime("XRTC ",&t);
      rtcSetDatetime(&t);
      delay(10);

      rtcGetDatetime(&t);
      printDatetime("rtc",&t);
    }
    return 1;
  }
  
  void rtcSetDatetime(datetime_t *t)
  {
      rtc_set_datetime(t);
  }

  void rtcGetDatetime( datetime_t *t)
  {
    rtc_get_datetime(t);
  }    

