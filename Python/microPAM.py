# microPAM.py contains ultilties to 
# - load data for microPAM derived software
#
import numpy as np
from numba import jit


#
# utilities
#--------------------------------------------------------
def find_chunk(hh, key):
    for ii in range(len(hh)):
        try:
            txt = hh[ii].tobytes().decode()
        except:
            continue
        if txt == key:
            break
    if ii < len(hh):
        return ii
    else:
        return -1


#--------------------------------------------------------
def wavInfo(hh):
    ii = find_chunk(hh[:128], 'fmt ')
    nch = hh[ii + 2] & 0xffff
    fs = hh[ii + 3]
    nbits = hh[ii + 5] >> 16
    return fs, nch, nbits


#
#--------------------------------------------------------
def decodeInfo(x):
    ii = find_chunk(x, 'INFO')
    if ii < 0: return {}
    if x[ii].tobytes().decode() != 'INFO':  return {}
    ii += 1
    info = {}
    while ii < len(x):
        key = x[ii].tobytes().decode()
        nd = x[ii + 1] // 4
        if nd == 0: break
        txt = x[ii + 2:ii + 1 + nd].tobytes().decode().strip('\00')
        info.update({key: txt})
        ii += 1 + nd
    return info

#
#------------------------------------------------------
def loadData(fileName):
    xx = np.fromfile(fileName, dtype='uint32')
    ii = find_chunk(xx, 'data') + 2
    if ii < 0: return [], []
    return xx[:ii], xx[ii:]

#
#------------------------------------------------------
def saveData(fileName, hh, xx):
    # save 'wav' style file (i.e. data with RIFF header)
    yy = np.concatenate((hh, xx)).astype('uint32')
    nn = xx.shape[0] * 4
    ii = find_chunk(hh, 'data')
    yy[ii + 1] = nn
    yy[1] = nn + (yy.shape[0] - 2) * 4
    yy.tofile(fileName)

#
#--------------------------------------------------------
@jit(nopython=True, cache=True)
def decodeBlock(out, inp, nd, nb, NX):
    # decode individual compressed block
    # nd expected block word count
    # nb compressed word size (number of bits)
    # NX expected word size (number of bits, typically 32)
    kk = 0
    nx = NX
    for ii in range(nd):
        nx -= nb
        if nx > 0:
            out[ii] = inp[kk] >> nx
        elif nx == 0:
            out[ii] = inp[kk]
            kk += 1
            nx = NX
        elif nx < 0:
            out[ii] = inp[kk] << (-nx)
            kk += 1
            nx += NX
            out[ii] |= (inp[kk] >> nx)
    # mask
    nb2 = np.int32(1 << nb)
    msk = np.uint32(nb2 - 1)
    out &= msk
    # extend sign bit
    nb1 = nb2 >> 1
    sgn = (out & nb1) > 0
    out[sgn] |= ~msk


#
#--------------------------------------------------------
#@jit(nopython=True, cache=True)
def decodeData(xx, blklen):
    # decompress data
    io = np.where(xx == 0xa5a5a5a5)[0]
    ncnt = len(io)
    data = np.zeros(ncnt * blklen, dtype='uint32')
    ii=0
    nx=0
    for ix in io:
        nb = np.int32(xx[ix+1])
        nd = xx[ix + 2]
        if (nb<24) and ((blklen * nb) == (nd * 32)): # cross-check for valid compressed block
            tmp0 = np.int32(xx[ix+3])
            k0 = ix + 4
            k1 = k0 + nd
            n0 = ii * blklen
            n1 = n0 + blklen
            ii += 1
            tmp = 0*data[n0:n1]
            decodeBlock(tmp, xx[k0:k1], blklen, nb, 32)
            # correct for offset
            tmp = (tmp.astype('int32') + tmp0).astype('uint32')
            data[n0:n1] = tmp.copy()
            nx=n1
    #
    data=data[:nx] #in case there are trailing zeros ('fake' or ignored buffers)
    return data.astype('int32')
#
#--------------------------------------------------------
def load_microPAM(fname):
    hh, xx = loadData(fname)
    #print(fname)
    fs, nch, nbits = wavInfo(hh)
    #print(fs, nch, nbits)
    ii = find_chunk(hh, 'LIST')
    if ii < 0:
        # plain wav file without LIST meta data
        cmpr = 0
        gain = 1
        preamp = 0  # dB
        Vref = 1  # V/MSB
    else:
        # there is an LIST field (decode and check if microPAM)
        info = decodeInfo(hh[ii:ii + hh[ii + 1] // 4])
        #for key, value in info.items():  print(f"{key}: {value}")

        if 'IKEY' in info.keys():
            config = info['IKEY'][:-1].split(';')  # last character is '.'
            if config[0] != fname[-28:-20]:
                kx=0
            else:
                kx=1
            gain = int(config[kx+4])
            shift = int(config[kx+6])
            cmpr = int(config[kx+7])
            blklen = int(config[kx+8])
            nblk = int(config[kx+9])
            #print(cmpr, gain, shift, blklen)
            #
            # have LC-MARE (very likely)
            preamp = 20  # dB
            Vref = 2.75  # V/MSB
        else:
            cmpr = 0
            gain = 1
            preamp = 0  # dB
            Vref = 1  # V/MSB

    # factor to scale from MSB to V
    scale = Vref / 10 ** ((preamp + gain) / 20)

    # check if compressed and decode if necessary
    if cmpr == 1:
        #print(blklen)
        data = decodeData(xx, blklen)  # nblk data blocks form 1 disk block
        scale /= 2 ** (nbits - 1 - shift)  # //LSB -> // MSB
    else:
        if nbits == 16:
            data = np.frombuffer(xx, dtype='int16')
        else:
            data = xx.astype('int32')
        scale /= 2 ** (nbits - 1)  # //LSB -> // MSB
    #
    # convert to V
    data = data * scale  # data is now  in V
    return fs, data


#
#--------------------------------------------------------
def load_LC_mare(fname):
    #
    #preamp = 20 # dB
    #Vref = 2.75 # V/MSB
    #scale=Vref/10**(preamp/20)
    #print(scale, 'V/MSB')
    return load_microPAM(fname)


def get_Info(fname):
    hh, xx = loadData(fname)
    ii = find_chunk(hh, 'LIST')
    if ii < 0: return {}
    info = decodeInfo(hh[ii:ii + hh[ii + 1] // 4])
    return info


def get_Voltage(fname):
    #get power supply voltage (microPAM)
    info = get_Info(fname)
    #print(info.keys())
    #for key, value in info.items():  print(f"{key}: {value}")

    if 'IKEY' in info.keys():
        config = info['IKEY'][:-1].split(';')  # last character is '.'
        return int(config[5])
    else:
        return 0
