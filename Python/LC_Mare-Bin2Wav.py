# convert from *.bin to *.wav 
import sys
from os.path import basename
import numpy as np
from microPAM import get_pamFileName,loadData,convertData

if len(sys.argv)==1:
    print("\nusage: python LC_Mare-Bin2Wav.py dest")
    print("where dest is '.' or any folder name (without trailing '/')")
    exit(1)
out_folder=sys.argv[1]

fname=get_pamFileName()
dest = out_folder+'/'+basename(fname)[:-3]+'wav'

hh,xx=loadData(fname)
data,fs,nch,scale=convertData(hh,xx,fname)

from scipy.io.wavfile import write
write(dest,fs,data.astype('int32'))
