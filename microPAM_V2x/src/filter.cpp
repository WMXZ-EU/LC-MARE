
#include <stdint.h>
#include <string.h>

#include <Arduino.h>
#include "mConfig.h"

#define BLOCK_SIZE NBUF_ACQ
#define MXBIT 20

int32_t cs[384];
int32_t sn[384];

int32_t tmp1[BLOCK_SIZE];
int32_t tmp2[BLOCK_SIZE];

int32_t inpc=0;
int32_t inps=0;

void setup_filter()
{
  for(int ii=0; ii<384;ii++) cs[ii]= int(cos(2*PI*ii/384.0)*(1<<MXBIT));
  for(int ii=0; ii<384;ii++) sn[ii]= int(sin(2*PI*ii/384.0)*(1<<MXBIT));
}

void apply_filter(int32_t *out, int32_t *inp, int32_t nch_in, int nch_out,int32_t nin, int32_t nf, int32_t nw, int32_t ns)
{

  for(int ii=0, kk=0; ii<nin; ii+=nch_in, kk +=nf)
  { int64_t xx = inp[ii];
    tmp1[ii] = (int32_t)((xx * cs[kk%384])>>MXBIT); 
    tmp2[ii] = (int32_t)((xx * sn[kk%384])>>MXBIT); 
  }
  tmp1[0] = ((nw-1)*inpc+tmp1[0]-inpc)/nw;
  tmp2[0] = ((nw-1)*inps+tmp2[0]-inps)/nw;


  for(int ii=1; ii<nin; ii++)
  {
    tmp1[ii] = ((nw-1)*tmp1[ii-1]+tmp1[ii])/nw;
    tmp2[ii] = ((nw-1)*tmp2[ii-1]+tmp2[ii])/nw;
  }

  inpc=tmp1[nin-1];
  inps=tmp2[nin-1];

  for(int ii=0, kk=0; ii<nin; ii+=ns, kk+=nch_out)
  { int64_t xx = tmp1[ii];
    int64_t yy = tmp2[ii];
    out[kk]=(int32_t)((xx*xx+yy*yy)>>16);
  }
}

void process_acq_init(void)
{ setup_filter(); 
}

void process_acq(int32_t * out, int32_t *inp, int32_t nbuf)
{ 
  for(int ii=0; ii<BLOCK_SIZE; ii+=BLOCK_SIZE)
  { apply_filter(out, inp, 2, 4, BLOCK_SIZE, 128, 8, 4);
    apply_filter(out, inp, 2, 4, BLOCK_SIZE, 16, 10, 5);
    apply_filter(out, inp, 2, 4, BLOCK_SIZE, 128, 10, 5);
    apply_filter(out, inp, 2, 4, BLOCK_SIZE, 16, 10, 5);
  }
}
