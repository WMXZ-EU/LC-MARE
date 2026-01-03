# microPAM.py contains ultilties to 
# - load data for microPAM derived software
#
import numpy as np
from numba import jit
#
# utilities
#------------------------------------------------------
def loadData(fileName):
    xx = np.fromfile(fileName, dtype='uint32')
    return xx[:128],xx[128:]
#
#------------------------------------------------------
def saveData(fileName,hh,xx):
    # save 'wav' style file (i.e. data with RIFF header)
    yy=np.concatenate((hh,xx)).astype('uint32')
    nn=xx.shape[0]*4
    yy[127]=nn
    yy[1]=nn+512-2*4
    yy.tofile(fileName)
#
#--------------------------------------------------------
@jit(nopython=True,cache=True)
def countBlocks(xx,nc):
    # count compressed data blocks
    ncnt=0
    nptr=0
    while nptr<len(xx):
        for ii in range(nc):
            ncnt += 1
            if xx[nptr] != 0xa5a5a5a5: break
            nptr=(nptr+4)+xx[nptr+2]
        nptr=(nptr//128+1)*128
    return ncnt
#
#--------------------------------------------------------
@jit(nopython=True,cache=True)
def decodeBlock(out,inp,nd,nb,NX):
    # decode individual compressed block
    # nd expected block word count
    # nb compressed word size (number of bits)
    # NX expected word size (number of bits, typically 32)
    kk=0
    nx=NX
    for ii in range(nd):
        nx -= nb
        if nx>0:
            out[ii] = inp[kk]>>nx
        elif nx==0:
            out[ii] = inp[kk]
            kk += 1
            nx=NX
        elif nx <0:
            out[ii] = inp[kk] << (-nx)
            kk += 1
            nx += NX
            out[ii] |= (inp[kk]>>nx)
    # mask
    nb2=np.int32(1<<nb)
    msk=np.uint32(nb2-1)
    out &=msk
    # extend sign bit
    nb1=nb2>>1
    sgn=(out & nb1)>0
    out[sgn] |= ~msk
#
#--------------------------------------------------------
@jit(nopython=True,cache=True)
def decodeData(xx,blklen,nc):
    # decompress data
    # nc number of blocks in single disk buffer (i.e. size of disk buffer nc*blklen words)
    ncnt=countBlocks(xx,nc)
    #print(ncnt, ncnt//nc)
    #
    # allocate complete data buffer
    data=np.zeros(ncnt*blklen,dtype='uint32')
    nptr=0
    for jj in range(ncnt//nc):
        for ii in range(nc):
            if xx[nptr] != 0xa5a5a5a5: break
            n0=(ii+nc*jj)*blklen
            n1=n0+blklen
            nb=xx[nptr+1]
            nk=xx[nptr+2] 
            tmp0 = xx[nptr+3:nptr+4].astype('int32')[0]

            k0=nptr+4
            k1=k0+nk
            tmp=data[n0:n1].copy()
            decodeBlock(tmp,xx[k0:k1],blklen,nb,32)
            # correct for offset
            tmp = (tmp.astype('int32')+tmp0).astype('uint32')
            data[n0:n1]=tmp.copy()
            # next block
            nptr=k1
        # round to next disk block (512 Bytes)
        nptr=(nptr//128+1)*128

    return data.astype('int32')
#
#--------------------------------------------------------
def wavInfo(hh):
    # decode wav header
    #print(hh[0].tobytes().decode()) # is RIFF
    #print(hh[2].tobytes().decode()) # is WAVE
    #print(hh[3].tobytes().decode()) # is fmt
    fs=hh[6]
    nch=hh[5]&0xffff
    nbits=hh[8]>>16
    #print(hh[126].tobytes().decode()) # is data
    ns=hh[127]    # is length of data block ()
    return fs,nch,nbits,ns
#
#--------------------------------------------------------
def toString(x):
    # convert wav meta data to pait of strings
    return x[0].tobytes().decode(),x[2:1+x[1]//4].tobytes().decode().strip('\x00')
#
#--------------------------------------------------------
def decodeInfo(hh):
    # decode wav header metafile
    #print(hh[9].tobytes().decode())
    #print(hh[11].tobytes().decode())
    info={}
    ik=12; 
    while(1):
        if hh[ik]==0: break
        key,text=toString(hh[ik:]); 
        info.update({key:text})
        ik +=hh[ik+1]//4+1;
    return info
#
#--------------------------------------------------------
def load_microPAM(fname,scale):
    hh,xx=loadData(fname)

    fs,nch,nbits,ns=wavInfo(hh)
    #print("fs",fs,"nch",nch,"nbits",nbits)

    info=decodeInfo(hh)
    #for key, value in info.items():  print(f"{key}: {value}")

    config=info['IKEY'][:-1].split(';') # last character is '.'
    gain=int(config[4])
    shift=int(config[6])
    cmpr=int(config[7])
    blklen=int(config[8])
    nblk=int(config[9])
    #print('gain',gain,'"shift"',31-shift,'nblk',nblk)

    # check if compressed and decode if necessary
    if cmpr==1:
        data=decodeData(xx,blklen,nblk) # nblk data blocks form 1 disk block
        scale /=2**(31-shift)   # //LSB -> // MSB
    else:
        data=xx.astype('int32')
        scale /=2**(31)         # //LSB -> // MSB
    #
    # convert to V
    scale /= 10**(gain/20)  # V/LSB
    data = data*scale       # data now  in V
    return fs,data
#
#--------------------------------------------------------
def loadLC_mare(fname):
    #
    preamp = 20 # dB
    Vref = 2.75 # V/MSB

    scale=Vref/10**(preamp/20)
    #print(scale, 'V/MSB')
    return load_microPAM(fname,scale)

