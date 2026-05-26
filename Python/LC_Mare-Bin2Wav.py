# convert from *.bin to *.wav 
import sys
from os.path import basename
from microPAM import get_pamFileName,loadData,convertData,saveData

if len(sys.argv)==1:
    print("\nusage: python LC_Mare-Bin2Wav.py dest")
    print("where dest is '.' or any folder name (without trailing '/')")
    exit(1)
out_folder=sys.argv[1]
meta=None
if len(sys.argv)==3:
    meta=sys.argv[2]

fname=get_pamFileName()
if fname==None: exit()
dest = out_folder+'/'+basename(fname)[:-3]+'wav'
#
hh,xx=loadData(fname)
data,fs,nch,scale=convertData(hh,xx,fname)

print(fname,'fs=',fs,'nch=',nch)
if meta=='meta':
    saveData(dest,hh,data)
else:
    from scipy.io.wavfile import write
    write(dest,fs,data.astype('int32').reshape(-1,nch))
