# convert from *.bin to *.wav 
import sys
from os.path import basename
from microPAM import get_pamFileName,loadData,decodeInfo,wavInfo

fname=get_pamFileName()
hh,xx=loadData(fname)
fs, nch, nbits, pcm = wavInfo(hh)
info=decodeInfo(hh)
#
print()
print(basename(fname))
print('fs    =',fs)
print('nch   =',nch)
print('nbits =',nbits)
print('Meta data')
for key, value in info.items():  print(f"{key}: {value}")