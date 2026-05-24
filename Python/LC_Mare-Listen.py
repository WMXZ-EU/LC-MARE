# listen only to *.bin
import numpy as np
from microPAM import get_pamFileName, load_microPAM
fname=get_pamFileName()
fs,data = load_microPAM(fname)
# scale to std
data /=(3*np.std(data))
# adjust volume
data /= 10
import  sounddevice as sd
sd.play(data, fs)
sd.wait()
#----------------------------