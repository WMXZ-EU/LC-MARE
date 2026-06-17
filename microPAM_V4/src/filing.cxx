#include "global.h"

uint16_t t_acq = T_ACQ;   // seconds
uint16_t t_on  = T_ON;    // minutes
uint16_t t_rep = T_REP;   // minutes (for continuous recording set t_rep < t_acq)

uint16_t h_rec[4] = {0,12,12,24};

char ISRC[40]={SRC_str}; //  Source
char ICMS[40]={CMS_str}; //  Organization
char IART[40]={ART_str}; // 'Artist' (creator)
char IPRD[40]={PRD_str}; // 'Product' (Activity)
char ISBJ[40]={SBJ_str}; // 'subject' (Area)
char INAM[40]={NAM_str}; // 'Name' (location id)
char startTime[40]={"2000-01-01 00:00:00"}; // Start Time
