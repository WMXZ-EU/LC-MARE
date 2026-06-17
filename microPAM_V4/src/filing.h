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
#endif