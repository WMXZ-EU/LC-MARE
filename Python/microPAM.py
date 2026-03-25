# microPAM.py contains ultilties to 
# - load data for microPAM derived software
#
import numpy as np
from numba import jit
#
# utilities
#--------------------------------------------------------
def find_chunk(hh,key):
    for ii in range(len(hh)):
        try:
            txt=hh[ii].tobytes().decode()
        except:
            continue
        if txt==key:
            break
    return ii
#--------------------------------------------------------
def wavInfo(hh):
    ii=find_chunk(hh[:128],'fmt ')
    nch=hh[ii+2]&0xffff
    fs=hh[ii+3]
    nbits=hh[ii+5]>>16
    return fs,nch,nbits
#
#--------------------------------------------------------
def decodeInfo(x):
    ii=find_chunk(x,'INFO')
    if x[ii].tobytes().decode() != 'INFO':
        return
    ii +=1
    info={}
    while ii<len(x):
        key=x[ii].tobytes().decode()
        nd=x[ii+1]//4
        txt=x[ii+2:ii+2+nd].tobytes().decode().strip('\00')
        info.update({key:txt})
        ii += 2+nd
    return info
#
#------------------------------------------------------
def loadData(fileName):
    xx = np.fromfile(fileName, dtype='uint32')
    ii=find_chunk(xx,'data')+2
    return xx[:ii],xx[ii:]
#
#------------------------------------------------------
def saveData(fileName,hh,xx):
    # save 'wav' style file (i.e. data with RIFF header)
    yy=np.concatenate((hh,xx)).astype('uint32')
    nn=xx.shape[0]*4
    ii=find_chunk(hh,'data')
    yy[ii+1]=nn
    yy[1]=nn+(yy.shape[0]-2)*4
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
def load_microPAM(fname):
    hh,xx=loadData(fname)

    fs,nch,nbits=wavInfo(hh)

    ii= find_chunk(hh,'LIST')
    info=decodeInfo(hh[ii:ii+hh[ii+1]//4])
    #for key, value in info.items():  print(f"{key}: {value}")

    if 'IKEY' in info.keys():
        config=info['IKEY'][:-1].split(';') # last character is '.'
        gain=int(config[4])
        shift=int(config[6])
        cmpr=int(config[7])
        blklen=int(config[8])
        nblk=int(config[9])
        #
        # have LC-MARE (very likely)
        preamp = 20 # dB
        Vref = 2.75 # V/MSB
    else:
        cmpr=0
        gain=1
        preamp = 0 # dB
        Vref = 1 # V/MSB

    scale=Vref/10**(preamp/20)

    # check if compressed and decode if necessary
    if cmpr==1:
        data=decodeData(xx,blklen,nblk) # nblk data blocks form 1 disk block
        scale /=2**(nbits-1-shift)   # //LSB -> // MSB
    else:
        data=xx.astype('int32')
        scale /=2**(nbits-1)         # //LSB -> // MSB
    #
    # convert to V
    scale /= 10**(gain/20)  # V/LSB
    data = data*scale       # data now  in V
    return fs,data
#
#--------------------------------------------------------
def load_LC_mare(fname):
    #
    #preamp = 20 # dB
    #Vref = 2.75 # V/MSB
    #scale=Vref/10**(preamp/20)
    #print(scale, 'V/MSB')
    return load_microPAM(fname)

