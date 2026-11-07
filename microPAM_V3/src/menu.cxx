/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
#include "Arduino.h"

#include "global.h"
#include "menu.h"
#include "rtc.h"
#include "rp2x.h"
#if MCU==T_4_1
  #include "teensy.h"
#endif
#include "adc.h"
#include "filing.h"
#include "queue.h"

void parameterPrint0(void)
{
  Serial.print("t_acq  (a) "); Serial.print(t_acq);  Serial.println(" sec");
  Serial.print("t_on   (o) "); Serial.print(t_on);   Serial.println(" min");
  Serial.print("t_rep  (r) "); Serial.print(t_rep);  Serial.println(" min");
  Serial.print("fsamp  (f) "); Serial.print(fsamp);  Serial.println(" Hz");
  Serial.print("again  (g) "); Serial.print(again);  Serial.println(" dB");
  Serial.print("Processing "); Serial.println(PROC_MODE);
  Serial.print("Voltage    "); Serial.println(analogRead(A1));
}

void parameterPrint(void)
{
  Serial.println("\n====================");
  Serial.println(Program);
  Serial.print("Version    "); Serial.println(Version);
  getUID();
  Serial.print("UID        "); Serial.println(uid_strng);
  #if MCU==T_4_1
    Serial.printf("PSRAM Size: %lu\r\n", getPSRAMSize());
  #else
    Serial.printf("PSRAM Size: %d\r\n", rp2040.getPSRAMSize());
  #endif
  Serial.printf("Queue Size: %lu\r\n", queueBytes());
  parameterPrint0();
}

/*************************************** input ******************************************/
static char *menuGetLine(void)
{
  static char buffer[40];
  while(!Serial.available()) continue;
  Serial.setTimeout(5000);
  int count = Serial.readBytesUntil('\n', buffer, 39);
  buffer[count] = 0;
  Serial.print("> "); Serial.println(buffer);
  return buffer;
}

static uint32_t menuGetInt(void)
{ int tmp = 0;
  sscanf(menuGetLine(), "%d", &tmp);
  return (uint32_t) tmp;
}

static void menuGetString(char *val)
{ sscanf(menuGetLine(), "%39s", val);
}

// expects yyyy-mm-dd hh:mm:ss (any single separators); returns 0 on short input
static uint16_t menuGetTime(datetime_t *t)
{
  char *buffer = menuGetLine();
  if(strlen(buffer) < 19) return 0;
  int v1, v2, v3, v4, v5, v6;
  char c1, c2, c3, c4, c5;
  sscanf(buffer, "%d%c%d%c%d%c%d%c%d%c%d",
                 &v1,&c1, &v2,&c2, &v3,&c3, &v4,&c4, &v5,&c5, &v6);
  t->year  = v1;
  t->month = v2;
  t->day   = v3;
  t->hour  = v4;
  t->min   = v5;
  t->sec   = v6;
  return 1;
}

static char waitChar(void)
{ while(!Serial.available()) delay(10);
  return Serial.read();
}

// hibernate until next multiple of 'unit' seconds, 'n' units ahead; reboot if n==0
static void hibernateUnits(uint32_t n, uint32_t unit, const char *name)
{
  if(n == 0) doReboot();
  Serial.print(" hibernating "); Serial.print(n); Serial.print(name);
  uint32_t alarm = (rtc_get()/unit + n)*unit;
  Serial.print(" ("); Serial.print(alarm - rtc_get()); Serial.println(" sec)");
  hibernate_until(alarm);
}

/*************************************** commands ***************************************/
static void getParameter(char ch)
{
  datetime_t t;
  switch(ch)
  { case 'a': Serial.print("a = "); Serial.println(t_acq); break;
    case 'o': Serial.print("o = "); Serial.println(t_on); break;
    case 'r': Serial.print("r = "); Serial.println(t_rep); break;
    case 'f': Serial.print("f = "); Serial.println(fsamp); break;
    case 'g': Serial.print("g = "); Serial.println(again); break;
    case 'u': Serial.print("u = "); Serial.println(uid_strng); break;
    case 'p': Serial.print("p = "); Serial.println(PROC_MODE); break;
    case 'm': Serial.print("m = "); Serial.println(PROC_MODE); break;
    case 'x': Serial.print("x = "); Serial.println(startTime); break;
    case 's': Serial.print("s = "); Serial.println(ISRC); break;
    case 'c': Serial.print("c = "); Serial.println(ICMS); break;
    case 'e': Serial.print("e = "); Serial.println(ISBJ); break;
    case 'd': XRTCgetDatetime(&t); printDatetime("d =", &t); break;
    case 'n': Serial.print("n = "); Serial.println(IART); break;
    case 'k': Serial.print("k = "); Serial.println(IPRD); break;
    case 'l': Serial.print("l = "); Serial.println(INAM); break;
    case '1': Serial.print("1 = "); Serial.println(h_rec[0]); break;
    case '2': Serial.print("2 = "); Serial.println(h_rec[1]); break;
    case '3': Serial.print("3 = "); Serial.println(h_rec[2]); break;
    case '4': Serial.print("4 = "); Serial.println(h_rec[3]); break;
  }
}

static void setParameter(char ch)
{
  datetime_t t;
  switch(ch)
  { case 'a': t_acq = menuGetInt(); break;
    case 'o': t_on  = menuGetInt(); break;
    case 'r': t_rep = menuGetInt(); break;
    case 'f': { uint32_t fs = menuGetInt();
                if(fs > FSAMP_MAX) fs = FSAMP_MAX;
                acqModifyFrequency(fs); } break;
    case 'g': again = menuGetInt(); setAGain(again); break;
    case 'n': menuGetString(IART); break;
    case 'k': menuGetString(IPRD); break;
    case 'l': menuGetString(INAM); break;
    case 's': menuGetString(ISRC); break;
    case 'c': menuGetString(ICMS); break;
    case 'e': menuGetString(ISBJ); break;
    case 'd': if(menuGetTime(&t)) { XRTCsetDatetime(&t); rtcSetDatetime(&t); } break;
    case '1': h_rec[0] = menuGetInt(); break;
    case '2': h_rec[1] = menuGetInt(); break;
    case '3': h_rec[2] = menuGetInt(); break;
    case '4': h_rec[3] = menuGetInt(); break;
    case 'x': menuGetString(startTime); break;
    default:  menuGetLine(); break;        // read-only or unknown: consume value
  }
}

// check RTC time and correct it if a new time is entered (only while stopped)
static void correctTime(void)
{
  while(Serial.available()) Serial.read();
  datetime_t t;
  rtcGetDatetime(&t);
  printDatetime("rtc", &t);
  Serial.println("If correct press return, otherwise enter correct date and time");
  if(menuGetTime(&t))
  { XRTCsetDatetime(&t);
    delay(10);
    rtcSetDatetime(&t);
    Serial.println("Corrected time is");
    printDatetime("rtc", &t);
  }
  else
  { Serial.println("Time has not been corrected");
  }
}

status_t menu(status_t status)
{
  if(!Serial || !Serial.available()) return status;

  char ch = Serial.read();
  switch(ch)
  { case 's': Serial.println("start"); status = DO_START; break;
    case 'e': Serial.print("stop ");   status = MUST_STOP; break;
    case 'p': parameterPrint(); break;
    case 'x': hibernateUnits(menuGetInt(), 3600, " hours"); break;
    case 'y': hibernateUnits(menuGetInt(), 60, " minutes"); break;
    case 'b': SD_stop(); doReboot(); break;
    case 'w': Serial.println(storeConfigToFile() ? "config.txt written" : "config.txt NOT written (no SD)"); break;
    case '?': getParameter(waitChar()); break;
    case '!': setParameter(waitChar()); break;
    case 'c': if(status == STOPPED) correctTime(); break;
    #if MCU==T_4_1
      case 'z': powerSteps(waitChar() - '0'); break;   // power steps for measurement (T41_POWER)
    #endif
  }
  return status;
}
