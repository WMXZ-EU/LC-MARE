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
    if ii < len(hh)-1:
        return ii
    else:
        return -1


#--------------------------------------------------------
def wavInfo(hh):
    ii = find_chunk(hh, 'fmt ')
    pcm = hh[ii+2] & 0xffff
    nch = hh[ii + 2] >>16
    fs = hh[ii + 3]
    nbits = hh[ii + 5] >> 16
    return fs, nch, nbits, pcm


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
        txt = x[ii + 2:ii + 2 + nd].tobytes().decode().strip('\00')
        info.update({key: txt})
        ii += 2 + nd
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
    yy = np.concatenate((hh, xx)).astype('uint32') # audacity does not like LIST
    #yy = np.concatenate((hh[:9], hh[126:128], xx[:,0])).astype('uint32')
    nn = xx.shape[0] * 4
    ii = find_chunk(yy, 'data')
    yy[ii + 1] = nn
    yy[1] = (yy.shape[0] - 2) * 4
    yy.tofile(fileName)

'''
#
#--------------------------------------------------------
@jit(nopython=True, cache=True)
def decodeBlock_(out, inp, nd, nb, NX):
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
def decodeData_(xx, blklen):
    # decompress data
    io = np.where(xx == 0xa5a5a5a5)[0]
    ncnt = len(io)
    data = np.zeros(ncnt * blklen, dtype='uint32')
    ii=0
    nx=0
    for ix in io:
        nb = np.int32(xx[ix+1])
        nd = xx[ix + 2]
        if (nb<20) and (nb>4) and ((blklen * nb) == (nd * 32)): # cross-check for valid compressed block
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

@jit(nopython=True, cache=True)
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
            out[ii] |= inp[kk]>>nx

def decodeData(xx, blklen):
    # decode integer-compressed data
    # find compressed blocks
    io = np.where(xx == 0xa5a5a5a5)[0]
    # check version
    if (blklen*xx[io[0]+1]) (32*xx[io[0]+2]):
        vesr=2
    elif (blklen*xx[io[0]+2]) (32*xx[io[0]+3]):
        vers=3

    ncnt = len(io)
    data = np.zeros(ncnt * blklen, dtype='uint32')
    if vers==2:
        ii=0
        for ix in io:
            nb = np.int32(xx[ix + 1])
            nk = xx[ix + 2]
            #
            if (blklen * nb) == (nk * 32):  # cross-check valid block
                tmp0 = np.int32(xx[ix+3])   # keep mean
                k0 = ix + 4
                k1 = k0 + nk
                n0 = ii * blklen
                n1 = n0 + blklen
                ii += 1
                tmp = 0*data[n0:n1]
                #
                decodeBlock(tmp, xx[k0:k1], blklen, nb, 32)
                # mask
                nb2 = np.int32(1 << nb)
                msk = np.uint32(nb2 - 1)
                tmp &= msk
                # extend sign bit
                nb1 = nb2 >> 1
                sgn = (tmp & nb1) > 0
                tmp[sgn] |= ~msk
                # correct for offset
                tmp = (tmp.astype('int32') + tmp0).astype('uint32')
                data[n0:n1] = tmp.copy()
        #
        data=data[:n1] #in case there are 'fake' or ignored buffers
    elif vers==3:
        ii=0
        for ix in io:
            nb = np.int32(xx[ix + 2])
            nk = xx[ix + 3]
            if (blklen * nb) == (nk * 32):  # cross-check valid block
                tmp0 = np.int32(xx[ix+4])   # keep fist value
                k0 = ix + 5
                k1 = k0 + nk
                n0 = ii * blklen
                n1 = n0 + blklen
                ii += 1
                tmp = 0*data[n0:n1]
                decodeBlock(tmp, xx[k0:k1], blklen, nb, 32)
                # mask
                nb2 = np.int32(1 << nb)
                msk = np.uint32(nb2 - 1)
                tmp &= msk
                # extend sign bit
                nb1 = nb2 >> 1
                sgn = (tmp & nb1) > 0
                tmp[sgn] |= ~msk
                #..... to be completed
    return data.astype('int32')
'''
'''#
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
    if ii < len(hh)-1:
        return ii
    else:
        return -1


#--------------------------------------------------------
def wavInfo(hh):
    ii = find_chunk(hh, 'fmt ')
    pcm = np.int32(hh[ii+2] & 0xffff)
    nch = np.int32(hh[ii + 2] >>16)
    fs = np.int32(hh[ii + 3])
    nbits = np.int32(hh[ii + 5] >> 16)
    return fs, nch, nbits, pcm


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

'''#
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
    return kk
#
#--------------------------------------------------------
def decodeData(xx, blklen,nch, vers):
    # decompress data
    io = np.where(xx == 0xa5a5a5a5)[0]
    ncnt = len(io)
    data = np.zeros(ncnt * blklen, dtype='uint32')
    if vers==2:
        # uses (header: MAGIC, nb,nd,meanSample), de-meaned samples (single channel)
        it=np.arange(len(io))
        ii=0
        for ix in io:
            nb = np.int32(xx[ix + 1])
            nk = xx[ix + 2]
            if (nb<25) and ((blklen * nb) == (32 * nk)): # cross-check for valid compressed block
                k0 = ix + 3
                k1 = k0 + nk
                n0 = ii * blklen
                n1 = n0 + blklen
                ii += 1

                tmp = 0*data[n0:n1]
                tmpo= xx[k0].copy()
                nkx=decodeBlock(tmp, xx[k0+1:k1+1], blklen, nb, 32)

                tmp = (tmp.astype('int32')+tmpo.astype('int32')).astype('uint32')
                data[n0:n1] = tmp.copy()
    elif vers==3:
        # uses (header: MAGIC, millis(),nb,nd,firstSamples), sample differences (nch channels)
        it=np.int32(xx[io+1])
        ii=0
        for ix in io:
            nb = np.int32(xx[ix + 2])
            nk = xx[ix + 3]
            if (nb<25) and ((blklen * nb) == (32 * nk)): # cross-check for valid compressed block
                k0 = ix + 4
                k1 = k0 + nk
                n0 = ii * blklen
                n1 = n0 + blklen
                ii += 1

                tmp = 0*data[n0:n1]
                tmpo= xx[k0:k0+nch].copy()
                nkx=decodeBlock(tmp, xx[k0+nch:k1+nch], blklen, nb, 32)

                itmp=tmp.copy().astype('int32')
                itmp[:nch]=tmpo
                for jj in range(nch,blklen): itmp[jj] += itmp[jj-nch]
                #
                data[n0:n1] = itmp.copy().astype('uint32')
    return it,data.astype('int32')

#--------------------------------------------------------
def convertData(hh,xx,fname,iprt=False):
    fs, nch, nbits, pcm = wavInfo(hh)

    vers=2
    ii = find_chunk(hh, 'LIST')
    if ii < 0:  # cannot find list
        # plain wav file without LIST meta data
        cmpr = 0
        gain = 1
        preamp = 0  # dB
        Vref = 1  # V/MSB
    else:
        # there is an LIST field (decode and check if microPAM)
        info = decodeInfo(hh[ii:ii+ hh[ii + 1] // 4])
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
            if iprt: print(cmpr, gain, shift, blklen)
            #
            # have LC-MARE (very likely)
            preamp = 20*np.log10(21)  # dB
            Vref   = 2.75  # V/MSB
        else:
            cmpr = 0
            gain = 0    # dB
            preamp = 0  # dB
            Vref = 1    # V/MSB
        #
        if 'ICMT' in info.keys():
            comment = info['ICMT'][:-1].split(';')
            if comment[0][:3]== 'Ver':
                vers=int(comment[0][8:].split('.')[0])

    # factor to scale from MSB to V
    scale = Vref / 10 ** ((preamp + gain) / 20)
    #print('gain',preamp+gain,'scale',scale)

    # check if microPAM compressed and decode if necessary
    if pcm==1:
        if cmpr == 1:
            it,data = decodeData(xx, blklen, nch, vers)
            scale *= 2 ** shift         # undo right shift
        else:
            if nbits == 16:
                data = np.frombuffer(xx, dtype='int16')
            else:
                data = xx.astype('int32')
        scale /= 2 ** (nbits - 1)  # //LSB -> // MSB
    else:
        data = np.frombuffer(xx, dtype='float32')
    #
    return data,fs,nch,scale

#--------------------------------------------------------
def load_microPAM(fname, iprt=False):
    hh, xx = loadData(fname)
    data,fs,nch,scale=convertData(hh,xx,fname,iprt)

    # convert to V
    data = data * scale             # data is now  in V
    data = data.reshape(-1,nch)
    return fs, data

#
#--------------------------------------------------------
def load_LC_mare(fname,iptr=False):
    return load_microPAM(fname,iptr)

#
#--------------------------------------------------------
def get_Info(fname):
    hh, xx = loadData(fname)
    ii = find_chunk(hh, 'LIST')
    if ii < 0: return {}
    info = decodeInfo(hh[ii:ii +1 + hh[ii + 1] // 4])
    return info

#
#--------------------------------------------------------
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

#
#--------------------------------------------------------
def dB(x,aa=10):
    return aa*np.log10(abs(x))

#
#--------------------------------------------------------
import tkinter as tk
from tkinter import filedialog

def get_pamFileName():
    try:
        # Create a hidden root window
        root = tk.Tk()
        root.withdraw()  # Hide the main Tkinter window

        # Ask the user to select a file
        file_path = filedialog.askopenfilename(
            title="Select a file",
            filetypes=[("uPAM files", "*.bin *.wav")]
        )

        # Destroy the root window after selection
        root.destroy()

        if not file_path:
            print("No file selected.")
            return None

        return file_path

    except Exception as e:
        print(f"Error loading file: {e}")
        return None
