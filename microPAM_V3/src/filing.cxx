/* microPAM_V3
 * Copyright (c) 2023/2024/2025/2026, Walter Zimmer
 * MIT license; see config.h for the full notice.
 */
#include "SPI.h"
#include "SdFat.h"

#include "global.h"
#include "rp2x.h"
#include "rtc.h"
#include "filing.h"
#include "adc.h"
#include "queue.h"
#include "menu.h"

uint16_t t_acq = T_ACQ;   // seconds
uint16_t t_on  = T_ON;    // minutes
uint16_t t_rep = T_REP;   // minutes

uint16_t h_rec[4] = {0,12,12,24};   // record in [h_rec[0],h_rec[1]) and [h_rec[2],h_rec[3]) hours

char ISRC[40] = {SRC_str};
char ICMS[40] = {CMS_str};
char IART[40] = {ART_str};
char IPRD[40] = {PRD_str};
char ISBJ[40] = {SBJ_str};
char INAM[40] = {NAM_str};
char startTime[40] = {"2000-01-01_00:00:00"};

/*************************************** microSD ****************************************/
#if SDFAT_FILE_TYPE != 3
  #error "SDFAT_FILE_TYPE != 3: edit SdFatConfig.h"
#endif

#if MCU==T_4_1
  #define SD_CONFIG SdioConfig(FIFO_SDIO)    // built-in microSD slot (4-bit SDIO)
#elif MCU==RP_2040 && SD_SDIO
  // 4-bit SDIO by PIO (SdFat PioSdioCard): CLK 18, CMD 19, DAT0-3 20-23; SD CLK =
  // clk_sys/(4*clkDiv) = SD_SDIO_MHZ (global.h); the PIO divider must be at least 1
  #if CLK_MULT*12 < 4*SD_SDIO_MHZ
    #error "SD_SDIO_MHZ needs clk_sys >= 4*SD_SDIO_MHZ (CLK_MULT)"
  #endif
  #if SD_SDIO_MHZ > 25
    // default speed allows 25 MHz; high speed (CMD6) gave CRC errors with the PIO driver,
    // which samples at the default-speed timing (3.5.9 test, 48 MHz)
    #error "SD_SDIO_MHZ above 25 MHz is out of the default-speed specification"
  #endif
  #define SD_CONFIG SdioConfig(PIN_SD_CLK, PIN_SD_CMD_MOSI, PIN_SD_DAT0_MISO, \
                               (CLK_MULT*12)/(4.0f*SD_SDIO_MHZ))
#elif MCU==RP_2040
  #define SD_CONFIG SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(SD_MULT*12), (SpiPort_t *) &SPI1)
#elif MCU==RP_2350
  #define SD_CONFIG SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(SD_MULT*12))
#endif

static SdFs sd;
static FsFile file;
static uint16_t have_sd = 0;

/*************************************** SD stand-by (CMD7) ******************************/
// Teensy 4.1 SDIO: between block writes the card is moved from the transfer state to the
// stand-by state with CMD7 (RCA 0, no response), the equivalent of CS high on SPI, and
// selected again with CMD7 (own RCA, R1b) before the next access (notes/SD_interface.md).
// SdFat knows nothing of it, so every SD access must be bracketed by sdWake()/sdSleep().
// The uSDHC registers are written directly; SdFat sends its commands by polling and arms
// the SDHC interrupt only during a transfer, so the two do not collide while the bus is idle.
#if MCU==T_4_1 && SD_CMD7
  static uint32_t sd_rca = 0;           // card RCA in bits 31:16
  static uint16_t sd_awake = 1;         // card in transfer state (as left by sd.begin)
  static uint32_t sd_cmd7_err = 0;      // failed selects

  // wait for command complete or command error; true if complete without error
  static bool sdCmdWait(void)
  { uint32_t t0 = micros();
    while (!(USDHC1_INT_STATUS & 0x000F0001)) { if (micros() - t0 > 10000) return false; }
    uint32_t st = USDHC1_INT_STATUS;
    USDHC1_INT_STATUS = st;             // write 1 to clear
    return (st & 1) && !(st & 0x000F0000);    // CC; no CTOE, CCE, CEBE, CIE
  }

  static bool sdCmd7(uint32_t arg, bool r1b)
  { uint32_t t0 = micros();
    while (USDHC1_PRES_STATE & 3) { if (micros() - t0 > 100000) return false; }  // CIHB, CDIHB
    USDHC1_CMD_ARG = arg;
    USDHC1_CMD_XFR_TYP = (7u << 24)     // CMDINX 7
        | (r1b ? ((3u << 16) | (1u << 19) | (1u << 20)) : 0);  // R1b with CRC and index check, or none
    bool ok = sdCmdWait();
    if (ok && r1b)
    { t0 = micros();                    // DAT0 busy released
      while (!(USDHC1_PRES_STATE & (1u << 24))) { if (micros() - t0 > 500000) return false; }
    }
    return ok;
  }

  static void sdGetRca(void)
  { sd.card()->status();                // CMD13 leaves the RCA in CMD_ARG
    sd_rca = USDHC1_CMD_ARG;
    Serial.printf("SD RCA 0x%04lX, VEND_SPEC 0x%08lX (FRC_SDCLK_ON %lu)\n",
                  sd_rca >> 16, USDHC1_VEND_SPEC, (USDHC1_VEND_SPEC >> 8) & 1);
  }

  static void sdSleep(void)
  { if(!sd_awake) return;
    if(!sd.card()->syncDevice()) return;  // end a multi-sector write, wait for programming
    sdCmd7(0, false);                   // no response: CC only
    sd_awake = 0;
  }

  static void sdWake(void)
  { if(sd_awake) return;
    if(!sdCmd7(sd_rca, true) && !sdCmd7(sd_rca, true))
    { if(!sd_cmd7_err++) Serial.println("SD select (CMD7) failed");
    }
    sd_awake = 1;   // accesses proceed in any case; SdFat reports its own errors
  }
#elif SD_CMD7 && MCU==RP_2040 && SD_SDIO
  #if defined(PIO_SDIO_HAS_CMD7)
  // RP2040 PIO SDIO with patches/SdFat_PioSdio_cmd7.patch (scripts\patch_sdfat.ps1): as on
  // the Teensy, the card goes to stand-by by CMD7 (RCA 0) after each block, and is selected
  // again by CMD7 (own RCA, R1b) before the next access.
  static uint16_t sd_awake = 1;         // card in transfer state (as left by sd.begin)
  static uint32_t sd_cmd7_err = 0;      // failed deselects or selects

  static inline SdioCard *sdioCard(void) { return static_cast<SdioCard *>(sd.card()); }

  static void sdSleep(void)
  { if(!sd_awake) return;
    if(sdioCard()->deselect()) sd_awake = 0;   // syncDevice(), then CMD7 (RCA 0)
    else if(!sd_cmd7_err++) Serial.println("SD deselect (CMD7) failed");
  }

  static void sdWake(void)
  { if(sd_awake) return;
    if(!sdioCard()->select() && !sdioCard()->select())
    { if(!sd_cmd7_err++) Serial.println("SD select (CMD7) failed");
    }
    sd_awake = 1;   // accesses proceed in any case; SdFat reports its own errors
  }

  static void sdGetRca(void)            // report RCA and test one deselect/select at boot
  { uint32_t rca = sdioCard()->rca();
    sdSleep();
    bool off = !sd_awake;
    sdWake();
    Serial.printf("SD CMD7 (PIO SDIO) RCA 0x%04lX: %s\n", (unsigned long)(rca >> 16),
                  (off && !sd_cmd7_err) ? "deselect/select ok" : "failed");
  }
  #else
  // RP2040 PIO SDIO, unpatched SdFat: PioSdioCard keeps its command interface private, so
  // CMD7 is not sent; syncDevice() ends the multi-sector write and waits for programming.
  static void sdSleep(void) { sd.card()->syncDevice(); }
  static inline void sdWake(void) {}
  static void sdGetRca(void)
  { Serial.println("SD PIO SDIO: CMD7 not available (run scripts\\patch_sdfat.ps1)");
  }
  #endif
#elif SD_CMD7
  // RP (SPI): the SD specification lists CMD7 as not supported in SPI mode, and with
  // SHARED_SPI SdFat already ends every multi-sector write and raises CS. CMD7 (RCA 0) is
  // nevertheless sent after each block, after syncDevice(), and its R1 response is kept;
  // a card that answers "illegal command" (R1 bit 2) is not sent it again.
  #if MCU==RP_2040
    #define SD_SPI SPI1
  #else
    #define SD_SPI SPI
  #endif
  static uint8_t  sd_cmd7_r1 = 0xFF;    // R1 of the last CMD7 (0xFF: no response)
  static uint16_t sd_cmd7_on = 1;       // cleared when the card rejects CMD7
  static uint32_t sd_cmd7_err = 0;      // CMD7 answered with an error bit or not at all

  static uint8_t sdCrc7(const uint8_t *b, int n)
  { uint8_t crc = 0;
    for(int ii = 0; ii < n; ii++)
    { uint8_t d = b[ii];
      for(int jj = 0; jj < 8; jj++)
      { crc <<= 1;
        if((d ^ crc) & 0x80) crc ^= 0x09;
        d <<= 1;
      }
    }
    return (crc << 1) | 1;
  }

  // CMD7 with argument arg over SPI; returns R1 (0xFF if none)
  static uint8_t sdSpiCmd7(uint32_t arg)
  { uint8_t cmd[6] = {0x40 | 7, (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
                      (uint8_t)(arg >> 8), (uint8_t)arg, 0};
    cmd[5] = sdCrc7(cmd, 5);
    SD_SPI.beginTransaction(SPISettings(SD_MULT*12000000, MSBFIRST, SPI_MODE0));
    digitalWrite(SD_CS, LOW);
    uint32_t t0 = millis();
    while(SD_SPI.transfer(0xFF) != 0xFF) if(millis() - t0 > 500) break;   // card ready
    for(int ii = 0; ii < 6; ii++) SD_SPI.transfer(cmd[ii]);
    uint8_t r1 = 0xFF;
    for(int ii = 0; ii < 10 && (r1 & 0x80); ii++) r1 = SD_SPI.transfer(0xFF);
    digitalWrite(SD_CS, HIGH);
    SD_SPI.transfer(0xFF);              // release DO, as SdFat does
    SD_SPI.endTransaction();
    return r1;
  }

  static void sdSleep(void)
  { if(!sd.card()->syncDevice()) return;  // end a multi-sector write, wait for programming
    if(!sd_cmd7_on) return;
    sd_cmd7_r1 = sdSpiCmd7(0);
    if(sd_cmd7_r1 & 0xFE)               // any error bit, or no response
    { sd_cmd7_err++;
      if(sd_cmd7_r1 & 0x04) sd_cmd7_on = 0;   // illegal command: give up
    }
  }

  static inline void sdWake(void) {}    // SPI: CS selects the card for each access

  static void sdGetRca(void)            // report: CMD7 response at boot
  { sdSleep();
    Serial.printf("SD CMD7 (SPI) R1 0x%02X: %s\n", sd_cmd7_r1,
                  (sd_cmd7_r1 == 0xFF) ? "no response" :
                  (sd_cmd7_r1 & 0x04) ? "illegal command, CMD7 off" :
                  (sd_cmd7_r1 & 0xFE) ? "error" : "accepted");
  }
#else
  static inline void sdGetRca(void) {}
  static inline void sdSleep(void) {}
  static inline void sdWake(void) {}
#endif

// file timestamp callback for SdFat (create and sync)
static void dateTime(uint16_t* date, uint16_t* time, uint8_t* ms10)
{
  datetime_t t;
  rtcGetDatetime(&t);
  *date = FS_DATE(t.year, t.month, t.day);
  *time = FS_TIME(t.hour, t.min, t.sec);
  *ms10 = 0;
}

/*************************************** WAV header *************************************/
static HdrStruct wav_hdr;
static char *wav_Info_ptr = wav_hdr.info;
static const char *wav_Info_end = wav_hdr.info + sizeof(wav_hdr.info);

// append a LIST/INFO sub-chunk; text is truncated if the header sector is full
static char *insertChunk(char *ptr, const char *id, const char *txt)
{
  int leno = strlen(txt);
  int avail = (wav_Info_end - ptr) - 8;
  if(avail < 4) return ptr;
  if(leno > avail) leno = avail & ~3;
  int len = ((leno+3)/4)*4;

  memcpy(ptr, id, 4); ptr += 4;
  memcpy(ptr, &len, 4); ptr += 4;
  memcpy(ptr, txt, leno); ptr += leno;
  for(int ii = leno; ii < len; ii++) *ptr++ = 0;
  return ptr;
}

static void wavInfoInit(void)
{
  char txt[40];
  char *wptr = wav_hdr.info;
  memset(wav_hdr.info, 0, sizeof(wav_hdr.info));
  sprintf(txt, "%s:%s", Program, Version);
  wptr = insertChunk(wptr, "ISFT", txt);
  wptr = insertChunk(wptr, "IGNR", "PAM");
  wptr = insertChunk(wptr, "ISRC", ISRC);
  wptr = insertChunk(wptr, "ICMS", ICMS);
  wptr = insertChunk(wptr, "IART", IART);
  wptr = insertChunk(wptr, "IPRD", IPRD);
  wptr = insertChunk(wptr, "ISBJ", ISBJ);
  wptr = insertChunk(wptr, "INAM", INAM);
  wav_Info_ptr = wptr;
}

static void wavHeaderInit(void)
{
  memcpy(wav_hdr.rId, "RIFF", 4);
  memcpy(wav_hdr.wId, "WAVE", 4);
  memcpy(wav_hdr.fId, "fmt ", 4);
  memcpy(wav_hdr.lId, "LIST", 4);
  memcpy(wav_hdr.iId, "INFO", 4);
  memcpy(wav_hdr.dId, "data", 4);

  wav_hdr.fLen = 16;
  wav_hdr.lLen = 512 - 13*4;    // LIST chunk: "INFO" + info[]
  wav_hdr.rLen = 512 - 2*4;     // updated at closing
  wav_hdr.dLen = 0;             // updated at closing

  wav_hdr.nFormatTag = 1;
  wav_hdr.nChannels = NCH;
  wav_hdr.nBitsPerSamples = MBIT;
  wav_hdr.nBlockAlign = NCH*MBIT/8;

  wavInfoInit();
}

static char datestring[80];
static char infotext[256];
static char *wavHeaderUpdate(uint32_t nbytes, int16_t vsens)
{
  // sampling frequency may have been changed at run time
  wav_hdr.nSamplesPerSec = fsamp;
  wav_hdr.nAvgBytesPerSec = fsamp*NCH*MBIT/8;

  char *wptr = wav_Info_ptr;
  memset(wptr, 0, wav_Info_end - wptr);
  wptr = insertChunk(wptr, "ICRD", datestring);
  // IKEY and ICMT follow microPAM_V5 field for field (NBUF_PROC = NBUF_ACQ, blocks per disk write = MD)
  sprintf(infotext, "%s; %4d; %4d; %4d; %4lu; %4lu; %6d; %3d; %3d; %4d; %3d; %3d; %4d; %4d; %4d; %4d; %s.",
                    uid_strng, t_acq, t_on, t_rep, fsamp/1000, again, vsens, SHIFT, PROC_MODE, NBUF_ACQ, MD, NAVG,
                    h_rec[0], h_rec[1], h_rec[2], h_rec[3], Version);
  wptr = insertChunk(wptr, "IKEY", infotext);
  sprintf(infotext, "Version: %s; MCU: %s; missed: acq %lu", Version, MCU_NAME, missed_acq);
  wptr = insertChunk(wptr, "ICMT", infotext);

  wav_hdr.dLen = nbytes;
  wav_hdr.rLen = nbytes + 512 - 2*4;
  return (char *)&wav_hdr;
}

uint16_t SD_init(void)
{
  getUID();
  #if MCU==T_4_1
    const int sd_id = BUILTIN_SDCARD;
  #elif MCU==RP_2040 && SD_SDIO
    const int sd_id = PIN_SD_CLK;   // SDIO: reported by its CLK pin
  #else
    const int sd_id = SD_CS;
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
  #endif

  int jj;
  for(jj = 0; jj < 5; jj++) if (sd.begin(SD_CONFIG)) break; else delay(1000);
  if(jj == 5)
  { Serial.printf("SD Storage %d failed or missing (SdFat error 0x%02X, data 0x%08lX)\n", sd_id,
                  sd.sdErrorCode(), (unsigned long)sd.sdErrorData());
    return 0;
  }

  #if MCU==RP_2040 && SD_SDIO
    Serial.printf("SD PIO SDIO %d MHz\n", SD_SDIO_MHZ);
  #endif
  Serial.printf("Storage %d", sd_id);
  Serial.print("; total clusters: "); Serial.print(sd.clusterCount());
  Serial.print(" free clusters: ");   Serial.print(sd.freeClusterCount());
  Serial.print(" clustersize: ");     Serial.print(sd.bytesPerCluster()/1024); Serial.println(" kByte");

  FsDateTime::callback = dateTime;
  pinMode(LED_BUILTIN, OUTPUT);   // flashes on disk writes

  wavHeaderInit();
  sdGetRca();
  have_sd = 1;
  return 1;
}

void SD_stop(void)
{ if(have_sd)
  { sdWake();
    storeConfigToFile();
    sd.card()->syncDevice();      // https://github.com/greiman/SdFat/issues/401
  }
}

/*************************************** disk writes ************************************/
static uint32_t mdt = 0;   // max write time (ms) since last status print
static uint32_t nbw = 0;   // max blocks written in one card cycle since last status print

static uint32_t write_disk(const int32_t *buffer, uint32_t nbytes)
{
  digitalWrite(LED_BUILTIN, HIGH);
  uint32_t to = millis();
  uint32_t ndat = file.write(buffer, nbytes);
  uint32_t dt = millis() - to;
  if(dt > mdt) mdt = dt;
  digitalWrite(LED_BUILTIN, LOW);
  return ndat;
}

static uint32_t storeData(const int32_t *buffer)
{
  #if PROC_MODE==0
    return write_disk(buffer, 4*NBLOCK);
  #else
    return write_disk(buffer, 4*buffer[NBLOCK-1]);   // last word: number of valid words
  #endif
}

/*************************************** logger *****************************************/
uint32_t diskBuffer[NBLOCK];

static uint32_t num_bytes_written = 0;
static char date_str[20];
static char time_str[20];
static uint32_t old_day  = 32;
static uint32_t old_hour = 24;
static uint32_t old_time = 0;

static char dayDir[40];
static char hourDir[10];
static const char extent[2][4] = {"wav", "bin"};
static int32_t logBuffer[9];

static void printStatus(uint32_t num_bytes_written, int16_t vsens)
{
  uint32_t num_samples = num_bytes_written / (4 * NCH);
  if(Serial)
  { Serial.printf("\t%5lu %8lu %3lu %2lu %4lu %2lu %6d:\t",
                  loop_count, num_samples, data_count, missed_acq, mdt, nbw, vsens);
    for(int ii = 0; ii < 9; ii++) Serial.printf("%8lx ", logBuffer[ii]);
    Serial.println();
  }
  data_count = 0;
  loop_count = 0;
  missed_acq = 0;
  mdt = 0;
  nbw = 0;
}

static status_t openFile(void)
{
  neo_pixel_show(10, 10, 10);

  datetime_t t;
  rtcGetDatetime(&t);
  sprintf(date_str, "%04d%02d%02d", t.year, t.month, t.day);
  sprintf(time_str, "%02d%02d%02d", t.hour, t.min, t.sec);
  sprintf(datestring, "%s_%s", date_str, time_str);    // ICRD in WAV header

  // directory structure /<uid>_<yyyymmdd>/<hh>/
  if((uint32_t)t.day != old_day)
  { sprintf(dayDir, "/%s_%s", uid_strng, date_str);
    if(!sd.exists(dayDir)) sd.mkdir(dayDir);
    sd.chdir(dayDir);
    old_day = t.day;
  }
  if((uint32_t)t.hour != old_hour)
  { sd.chdir(dayDir);
    sprintf(hourDir, "%02d", t.hour);
    if(!sd.exists(hourDir)) sd.mkdir(hourDir);
    sd.chdir(hourDir);
    old_hour = t.hour;
  }

  char fileName[80];
  sprintf(fileName, "%s_%s_%s.%s", uid_strng, date_str, time_str, extent[PROC_MODE]);
  file = sd.open(fileName, FILE_WRITE);
  if(!file)
  { neo_pixel_show(0, 0, 10);
    return JUST_STOPPED;
  }
  Serial.print(fileName); Serial.print("; ");
  file.write(&wav_hdr, 512);    // placeholder, rewritten at closing
  num_bytes_written = 0;
  // t_acq phase at opening: without it a start after a stop compares the first block with
  // the phase of the last block before the stop and may close the new file after one block
  old_time = rtc_get() % t_acq;
  neo_pixel_show(0, 0, 0);
  return RECORDING;
}

static void closeFile(const int32_t *buffer)
{
  int16_t vsens = analogRead(A1);
  char *wav_header = wavHeaderUpdate(num_bytes_written, vsens);

  uint64_t fpos = file.curPosition();
  file.seekSet(0);
  file.write((const uint8_t *)wav_header, 512);
  file.seekSet(fpos);
  file.close();

  // first words of last block for status print (skip sync word if compressed)
  memcpy(logBuffer, &buffer[PROC_MODE], 9*4);
  printStatus(num_bytes_written, vsens);
}

// sleep if outside recording hours or outside the on-period of the duty cycle
static void checkHibernate(uint32_t tt)
{
  uint32_t tto = tt - tt % (24*3600);   // start of day
  uint32_t hhx = (tt % (24*3600)) / 3600;
  // record in hours h_rec[0] <= h < h_rec[1] and h_rec[2] <= h < h_rec[3]
  if(hhx < h_rec[0])                        hibernate_until(tto + h_rec[0]*3600);
  if((hhx >= h_rec[1]) && (hhx < h_rec[2])) hibernate_until(tto + h_rec[2]*3600);
  if(hhx >= h_rec[3])                       hibernate_until(tto + (24+h_rec[0])*3600);

  if(t_rep > t_on)
  { uint32_t ttm = tt/60;
    uint32_t dt2 = ttm % t_rep;
    Serial.printf("%d %d %lu %lu %d\n", t_acq, t_rep, ttm, dt2, t_on);
    if(dt2 >= t_on)
    { adc_exit();
      uint32_t alarm = ((ttm/t_rep)+1)*t_rep*60;
      Serial.printf("alarm %lu %lu %lu %lu\n", dt2, ttm, tt, alarm);
      hibernate_until(alarm);
    }
  }
}

static status_t loggerBlock(status_t status, const int32_t *buffer);

// one card cycle: wake, write up to SD_DRAIN queued blocks, stand-by. A backlog (card in a
// slow phase) is then written as one longer write command instead of one command per block.
status_t logger(status_t status)
{
  const int32_t *buffer = (const int32_t *) diskBuffer;
  #if SD_CMD7
    uint32_t to = millis();
    sdWake();
  #endif
  uint32_t nb = 0;
  do
  { pullQueue(diskBuffer);
    status = loggerBlock(status, buffer);
    data_count++;
    nb++;
  } while((status == RECORDING) && (nb < SD_DRAIN) && (getQueueCount() > 0));
  if(nb > nbw) nbw = nb;
  #if SD_CMD7
    sdSleep();          // card to stand-by until the next cycle (waits for programming)
    uint32_t dt = millis() - to;
    if(dt > mdt) mdt = dt;   // mdt then includes select, programming and deselect
  #endif
  return status;
}

static status_t loggerBlock(status_t status, const int32_t *buffer)
{
  if(status == CLOSED)
  { status = openFile();
    if(status != RECORDING) return status;
  }

  if((status == RECORDING) || (status == MUST_STOP))
  { num_bytes_written += storeData(buffer);

    // close file at each multiple of t_acq seconds, or on request
    uint32_t tt = rtc_get();
    uint32_t tmp_time = tt % t_acq;
    if((tmp_time < old_time) || (status == MUST_STOP))
    { closeFile(buffer);
      if(status == MUST_STOP)
      { Serial.print(" stopped ");
        status = JUST_STOPPED;
      }
      else
      { status = CLOSED;
        checkHibernate(tt);
      }
    }
    old_time = tmp_time;
  }
  return status;
}

/*************************************** config.txt *************************************/
// one parameter per line: "!<key> <value>", terminated by '#' or ';' (comment may follow)
#define CONFIG_FILE "/config.txt"
#define MAX_ENTRIES 32

int storeConfigToFile(void)
{
  if(!have_sd) return 0;
  sdWake();
  FsFile cfg = sd.open(CONFIG_FILE, (O_RDWR | O_CREAT | O_TRUNC));
  if(!cfg) return 0;
  cfg.printf("# configuration file\n");
  cfg.printf("# should end with '#' or ';' comment may follow\n");
  cfg.printf("#\n");
  // layout identical to microPAM_V5 (key order and separators)
  cfg.printf("!a %d  # file size (sec)\n", t_acq);
  cfg.printf("!o %d  # on time (min)\n", t_on);
  cfg.printf("!r %d  # repetition interval (min)\n", t_rep);
  cfg.printf("!f %lu\t# sampling frequency (kHz)\n", fsamp/1000);
  cfg.printf("!g %lu\t# analog gain (dB)\n", again);
  cfg.printf("!m %d\t# project mode (0: raw; 1:compressed)\n", PROC_MODE);
  cfg.printf("!s %s\t# (ISRC) source with sensitivity\n", ISRC);
  cfg.printf("!c %s\t# (ICMS) commissioning organization\n", ICMS);
  cfg.printf("!n %s\t# (IART) name of operator (creator)\n", IART);
  cfg.printf("!p %s\t# (IPRD) project\n", IPRD);
  cfg.printf("!e %s\t# (ISBJ) area\n", ISBJ);
  cfg.printf("!l %s\t# (INAM) location id\n", INAM);
  cfg.printf("!1 %d\t# h_rec[0]\n", h_rec[0]);
  cfg.printf("!2 %d\t# h_rec[1]\n", h_rec[1]);
  cfg.printf("!3 %d\t# h_rec[2]\n", h_rec[2]);
  cfg.printf("!4 %d\t# h_rec[3]\n", h_rec[3]);
  cfg.printf("!x %s  # start time yyyy-mm-dd_hh:mm:ss\n", startTime);
  cfg.close();
  return 1;
}

static uint16_t scan16(const char *txt) { int tmp = 0; sscanf(txt, "%d", &tmp); return (uint16_t) tmp; }

int16_t loadConfigfromFile(void)
{
  static char configText[20*80];
  const int nmax = sizeof(configText) - 1;
  int configIndex[MAX_ENTRIES];

  sdWake();
  FsFile cfg = sd.open(CONFIG_FILE);
  if(!cfg) return 0;
  int imax = 0;
  while (cfg.available() && (imax < nmax)) configText[imax++] = cfg.read();
  configText[imax] = 0;
  cfg.close();

  int jmax = 0;
  for(int ii = 0; ii < imax && jmax < MAX_ENTRIES; ii++) if(configText[ii] == '!') configIndex[jmax++] = ii;

  for(int ii = 0; ii < jmax; ii++)
  { int i1 = configIndex[ii] + 1;
    int i2 = i1 + 1;
    int i2max = (i1+80 < imax) ? i1+80 : imax;
    while(i2 < i2max && configText[i2] != '#' && configText[i2] != ';' && configText[i2] != '\n') i2++;
    configText[i2] = 0;
    const char *txt  = &configText[i1];
    const char *txt2 = &txt[2];

    switch(txt[0])
    { case 'a': t_acq = scan16(txt2); break;
      case 'o': t_on  = scan16(txt2); break;
      case 'r': t_rep = scan16(txt2); break;
      case 'f': { uint32_t fs = scan16(txt2)*1000;
                  if(fs > FSAMP_MAX) fs = FSAMP_MAX;
                  acqModifyFrequency(fs); } break;
      case 'g': again = scan16(txt2); break;            // applied by adc_init()
      case 'm': break;                                   // PROC_MODE is compile-time; reported only
      case 's': sscanf(txt2, "%39s", ISRC); break;
      case 'c': sscanf(txt2, "%39s", ICMS); break;
      case 'n': sscanf(txt2, "%39s", IART); break;
      case 'p': sscanf(txt2, "%39s", IPRD); break;
      case 'e': sscanf(txt2, "%39s", ISBJ); break;
      case 'l': sscanf(txt2, "%39s", INAM); break;
      case '1': h_rec[0] = scan16(txt2); break;
      case '2': h_rec[1] = scan16(txt2); break;
      case '3': h_rec[2] = scan16(txt2); break;
      case '4': h_rec[3] = scan16(txt2); break;
      case 'x': sscanf(txt2, "%39s", startTime); break;
    }
  }
  return jmax;
}

int configShow(void)
{
  if(loadConfigfromFile() <= 0) return 0;
  Serial.println("config loaded");
  parameterPrint0();
  Serial.print("ISRC "); Serial.println(ISRC);
  Serial.print("ICMS "); Serial.println(ICMS);
  Serial.print("IART "); Serial.println(IART);
  Serial.print("IPRD "); Serial.println(IPRD);
  Serial.print("ISBJ "); Serial.println(ISBJ);
  Serial.print("INAM "); Serial.println(INAM);
  wavInfoInit();
  return 1;
}
