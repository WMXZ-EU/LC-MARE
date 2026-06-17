#ifndef ACQ_H
#define ACQ_H

  void adc_init(void);
  void adc_exit(void);

  extern uint32_t again;  // ADC gain
  void setAGain(int8_t again);

#endif 
