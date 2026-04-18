
import tkinter as tk
from tkinter import filedialog

def get_fileName():
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

fname=get_fileName()
print(fname)

from microPAM import load_microPAM, dB

fs,data = load_microPAM(fname)
if 0:
    Vmax  =  12.277 # 24dBu ZOOM F3
    Sens  = -168    # SQ 26-08
    Senso = -200    # reference sensitivity (microPAM code)
    rescale = 10.0**((Senso-Sens)/20)/Vmax
    data *= rescale

import numpy as np
data=np.diff(data,axis=0)
td = np.arange(data.shape[0])/fs
print(fs,data.shape[0]/fs)

if 0:
    print('Playing')
    import sounddevice as sd

    sd.play(np.double(data)*2**8, fs)
    sd.wait()
    print('Done')

# calibrate data
cal=-80 # dB//1V/Pa         # 1 Pa generates about 10^-4 V (-80 dB) (-200 dB//uPa)
data = data/10**(cal/20)

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
plt.show(block=False)

#
U=np.mean(q,axis=1)
fu=f.copy()
#
nw=64*1024
f,q=welch(data[:,0],fs=fs,window='hann',nperseg=nw,noverlap=nw//2,nfft=nw*2,scaling='density')
#
plt.figure()
plt.plot(f,dB(q))
plt.plot(fu,dB(U))
plt.ylabel('dB//1Pa$^2$/Hz')
#plt.xlim(0,200)
plt.grid(True)
plt.xscale('log')
plt.show()
