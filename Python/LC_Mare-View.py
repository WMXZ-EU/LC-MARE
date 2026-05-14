
from microPAM import get_pamFileName, load_microPAM, dB

fname=get_pamFileName()
print(fname)

fs,data = load_microPAM(fname)
if 0:
    # relate other sensor to microPAM
    Vmax  =  12.277 # 24dBu ZOOM F3
    Sens  = -168    # SQ 26-08
    Senso = -206    # reference sensitivity (microPAM code)
    rescale = 10.0**((Senso-Sens)/20)/Vmax
    data *= rescale

import numpy as np
#data=np.diff(data,axis=0)
td = np.arange(data.shape[0])/fs
print(fs,data.shape[0]/fs)

# calibrate data (LC-Mare)
sens=-86 # dB//1V/Pa         # assume 1 Pa generates 50 E-6 V (10**(-86/20)) (sensitivity -206 dB//1V/uPa)
data /= 10**(sens/20)

if 0:
    print('Playing')
    import sounddevice as sd

    sd.play(data/50, fs)
    #sd.wait()
    print('Done')

#
# spectrogram
from scipy.signal import spectrogram, welch
nw=512

f,t,q=spectrogram(data[:,0],fs=fs,window='hann',nperseg=nw,noverlap=nw//2,nfft=nw*2,scaling='density')

Q=dB(q)

import matplotlib.pyplot as plt

fig,axs=plt.subplots(2,1,figsize=(10,7),sharex=True, layout='constrained')

axs[0].plot(td,data)
axs[0].set_ylabel('Pressure [Pa]')

qmax=np.max(Q)
clim=[qmax-60,qmax]
ext=[t[0],t[-1],f[0]/1000,f[-1]/1000]
#
img=axs[1].imshow(Q, aspect='auto',origin='lower', extent=ext,cmap='jet',clim=clim)
plt.colorbar(img)
plt.xlabel('Time [s]')
plt.ylabel('Frequency [kHz]')
plt.show(block=False)

#
# average power spectral density
nw=64*1024
fw,qw=welch(data[:,0],fs=fs,window='hann',nperseg=nw,noverlap=nw//2,nfft=nw*2,scaling='density')
#
qu=np.mean(q,axis=1)
fu=f.copy()
#
plt.figure(figsize=(10,7))
plt.plot(fw,dB(qw))
plt.plot(fu,dB(qu))
plt.ylabel('dB//1Pa$^2$/Hz')
plt.xlabel('Frequency [Hz]')
plt.grid(True)
plt.xscale('log')
plt.show(block=False)

plt.show()