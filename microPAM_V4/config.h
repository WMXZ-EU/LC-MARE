#ifndef CONFIG_H
#define CONFIG_H

  //------------------------------------
  // acquisition constants
  //------------------------------------
  #define T_ACQ     60  // seconds
  #define T_ON      1   // minutes
  #define T_REP     0   // minutes (for continuous recording set t_rep < t_acq)

  // definitions for acquisition and filing (default to highest useful frequency)
  #define FSAMP 192000  // will be limited to 96000 if RP2040
  //
  #define PROC_MODE      2   // 0 Wav raw file; 1 compress 

  //for filing meta data
  #define SRC_str "LC17"     //  Source
  #define CMS_str "WMXZ"     //  Organization
  #define ART_str "WMXZ"     // 'Artist'  (creator)
  #define PRD_str "microPAM" // 'Product' (Activity)
  #define SBJ_str "______"   // 'subject' (Area)
  #define NAM_str "Test"     // 'Name'    (location id)

#endif
