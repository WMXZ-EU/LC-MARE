#ifndef PROCESS_H
#define PROCESS_H

class Queue
{ int16_t head,tail,busy;
  int32_t cnt;

  public:
  Queue() {reset();}

  void reset(void);
  int push(uint32_t *data, int ndat);
  int pull(uint32_t *data);
  int available(void);
};

extern Queue queue;

void process(int32_t * buffer);

extern uint32_t acq_missed;
extern uint32_t acq_count;
extern uint32_t proc_time;
#endif