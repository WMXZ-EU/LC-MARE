import os
from os.path import basename

import threading

import numpy as np
from scipy.signal import spectrogram
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import (FigureCanvasTkAgg, NavigationToolbar2Tk)

import tkinter as tk
from tkinter import ttk, filedialog
from tkinter.scrolledtext import ScrolledText

import time
from datetime import datetime

import serial
import serial.tools.list_ports

import sounddevice as sd

from urllib.request import urlopen
from shutil import copyfileobj

from microPAM import get_pamFileName, loadData, convertData,saveData, decodeInfo, wavInfo, load_microPAM, dB

# info text for welcome frame
into_text = \
"""This user interface is for basic microPAM LC-Mare activities.
The programs work for both compresses (*.bin) or wav (*.wav) files

The different tabs allow
- Info:\tinspecting the basic parameters of a data file
- View:\tplot time series and spectrogram.
        \tlisten to the selected (zoomed) data
- Convert:\tconverts *.bin to *.wav files (either single file of multiple files in a folder)
- Config:\tGUI for checking and updating basic configuration parameters
- Upload:\tallows update of firmware (can fetch latest version from GitHub)
"""

_fnt=('Ariel',12)

def get_pamInfo():
    fname = get_pamFileName()
    hh, xx = loadData(fname)
    fs, nch, nbits, pcm = wavInfo(hh)
    info = decodeInfo(hh)
    info_text= basename(fname) + '\n'
    info_text += "fs    = " + str(fs) + '\n'
    info_text += "nch    = " + str(nch) + '\n'
    info_text += "nbits  = " + str(nbits) + '\n'
    info_text += 'Meta data' + '\n'
    for key, value in info.items(): info_text += f"{key}: {value}" + '\n'
    return info_text


def getComPort():
    s=serial.tools.list_ports.comports(True)
    for ii in range(len(s)):
        if (s[ii].vid==0x239a):
            if (s[ii].pid==0x815d) | (s[ii].pid==0x814f): # adafruit adalogger rp2040 or feather rp2350
                return s[ii].device
        if (s[ii].vid == 0x16C0): # PJRC
            return s[ii].device
        return None

def getDevPid():
    s=serial.tools.list_ports.comports(True)
    for ii in range(len(s)):
        if (s[ii].vid==0x239a):
            if (s[ii].pid==0x815d):
                return 'rp2040'
            if (s[ii].pid==0x814f):
                return 'rp2350'
        if (s[ii].vid == 0x16C0): # PJRC
            return 'Teensy4.1'
        return None

def openSerial():
    com = getComPort()
    try:
        ser=serial.Serial(com, timeout=0.1)
    except Exception as e:
        print(e)
        print("please close the serial port")
        return None
    else:
        return ser

#======================================================================================
class WelcomeFrame(ttk.Frame):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        ttk.Label(self,text='microPAM LC_Mare').pack(pady=10)

        t=tk.Text(self, height = 18, width = 80,
                  relief=tk.FLAT, borderwidth=1, font=("Helvetica", 14))
        t.pack(padx=10,pady=10)
        t.insert('1.0', into_text)
        t.config(state=tk.DISABLED)

#======================================================================================
class InfoFrame(ttk.Frame):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        tk.Label(self,text="Information of microPAM LC_Mare files",font=_fnt).pack(pady=10)
        tk.Button(self, text="Open File", command=self.get_info,font=_fnt).pack(pady=10)
        self.infoText=tk.Text(self, height = 18, width = 60,
                              relief=tk.FLAT, borderwidth=1, font=("Helvetica", 12))
        self.infoText.pack(padx=10,pady=10)

    def get_info(self):
        self.infoText.insert('1.0',get_pamInfo())

#======================================================================================
class ViewFrame(ttk.Frame):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        tk.Label(self,text='View and listen to data',font=_fnt).pack(pady=10)
        frame = tk.Frame(self,width=300,height=30)
        frame.pack(padx=10,pady=10)

        self.open = tk.Button(frame, text="Open File", command=self.do_view,font=_fnt).pack()
        self.listen = tk.Button(frame, text="Listen", command=self.do_listen,font=_fnt).pack(padx=10)
        self.data=[]
        self.init_view()

    def init_view(self):
        self.fig, self.axs = plt.subplots(2, 1, figsize=(12, 7),
                                        sharex=True,#sharey=True,
                                        layout='constrained')#, tight_layout=True)

        # containing the Matplotlib figure
        self.canvas = FigureCanvasTkAgg(self.fig)

        self.canvas = FigureCanvasTkAgg(self.fig, master=self)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)
        nav = NavigationToolbar2Tk(self.canvas, self)
        nav.config(bg='white')
        nav.update()

    def do_view(self):
        fname = get_pamFileName()
        if fname == None: return
        print(fname)

        self.fs, self.data, it = load_microPAM(fname,True)
        self.plot_view(self.fs,self.data)

    def plot_view(self,fs,data):
        td = np.arange(data.shape[0])/fs
        print(fs,data.shape[0]/fs)

        # calibrate data (LC-Mare)
        sens=-86 # dB//1V/Pa         # assume 1 Pa generates 50 E-6 V (10**(-86/20)) (sensitivity -206 dB//1V/uPa)
        data /= 10**(sens/20)

        # spectrogram
        nw=512

        f,t,q=spectrogram(data[:,0],fs=fs,window='hann',nperseg=nw,noverlap=nw//2,nfft=nw*2,scaling='density')

        Q=dB(q)

        #fig,axs=plt.subplots(2,1,figsize=(10,7),sharex=True, layout='constrained')
        axs = self.axs

        axs[0].plot(td,data)
        axs[0].grid(True)
        axs[0].set_ylabel('Pressure [Pa]')

        qmax=np.max(Q)
        clim=[qmax-60,qmax]
        ext=[t[0],t[-1],f[0]/1000,f[-1]/1000]
        #
        img=axs[1].imshow(Q, aspect='auto',origin='lower', extent=ext,cmap='jet',clim=clim)
        plt.colorbar(img)
        axs[1].set_xlabel('Time [s]')
        axs[1].set_ylabel('Frequency [kHz]')
        self.canvas.draw()

    def do_listen(self):
        ax=self.axs[0]
        xlim=ax.get_xlim()
        data=self.data
        td = np.arange(data.shape[0])/self.fs
        isel=(td>xlim[0]) & (td<xlim[1])
        data=data[isel]

        # scale to std
        data /= (3 * np.std(data))
        # adjust volume
        data /= 10
        sd.play(data, self.fs)
        sd.wait()


#======================================================================================
class ConfigFrame(ttk.Frame):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.timer_is_running=0
        self.task_is_running=0
        self.init_gui()

    def __del__(self):
        self.timer_is_running=0
        self.task_is_running=0

    def init_gui(self):
        label1 = tk.Label(self,text="PC:",font=("Helvetica", 18))
        yo=10
        label1.place(x=250,y=yo)
        self.pcClocklabel = ttk.Label(self,text="", foreground="Red", font=("Helvetica", 18))
        self.pcClocklabel.place(x=320,y=yo)
        self.timer_is_running=1
        self.update_clock()

        label2 = tk.Label(self,text="MCU:",font=("Helvetica", 18))
        label2.place(x=240,y=yo+40)
        self.mcuClocklabel = tk.Entry(self,text="", fg="Black", font=("Helvetica", 18))
        self.mcuClocklabel.place(x=320,y=yo+40)

        xo=100
        yo=yo+40
        self.sernum_edit = self.mEntry("SerNum:",xo,yo,10,100);

        xo=80
        yo=110
        dxo=230
        ii=0
        self.b_edit = self.mEntry("Author:", xo+ii*dxo,yo,10,80); ii+=1
        self.k_edit = self.mEntry("Project:",xo+ii*dxo-5,yo,10,85); ii+=1
        self.n_edit = self.mEntry("Location:",xo+ii*dxo+5,yo,10,100); ii+=1

        xo=140
        yo=170
        ii=0
        self.fsamp_edit = self.mEntry("fsamp (Hz):",xo,yo+ii*40,6,130); ii+=1
        self.proc_edit  = self.mEntry("proc (0/1):", xo,yo+ii*40,1,130); ii+=1
        self.shift_edit = self.mEntry("shift:",xo,yo+ii*40,2,130); ii+=1
        self.again_edit = self.mEntry("again (dB):",xo,yo+ii*40,2,130); ii+=1

        xo=360
        yo=170
        ii=0
        self.t_acq_edit = self.mEntry("t_acq (s):",xo,yo+ii*40,5,110); ii+=1
        self.t_on_edit  = self.mEntry("t_on (m):", xo,yo+ii*40,5,110); ii+=1
        self.t_rep_edit = self.mEntry("t_rep (m):",xo,yo+ii*40,5,110); ii+=1
        ii=0
        xo += 170
        self.h_1_edit = self.mEntry("h_1 (h):",xo,yo+ii*40,2,90); ii+=1
        self.h_2_edit = self.mEntry("h_2 (h):",xo,yo+ii*40,2,90); ii+=1
        self.h_3_edit = self.mEntry("h_3 (h):",xo,yo+ii*40,2,90); ii+=1
        self.h_4_edit = self.mEntry("h_4 (h):",xo,yo+ii*40,2,90); ii+=1
        yo += 30
        self.h_start_edit = self.mEntry("start Time :",xo-320,yo+ii*40,18,130);

        # temporary disabling input
        self.shift_edit.configure(state="disabled")
        # end disabling input

        # create buttons
        xm=600
        ym=170
        dym=60
        ii=0
        #tk.Button(self, text="Exit", command=self.clickExitButton, font=("Helvetica", 18)).place(x=xm, y=10)
        tk.Button(self, text="Load", command=self.clickLoadButton, font=("Helvetica", 18)).place(x=xm, y=ym+ii*dym); ii+=1
        tk.Button(self, text="Sync", command=self.clickSyncButton, font=("Helvetica", 18)).place(x=xm, y=ym+ii*dym); ii+=1
        tk.Button(self, text="Save", command=self.clickSaveButton, font=("Helvetica", 18)).place(x=xm, y=ym+ii*dym); ii+=1

        self.restartButton = tk.Button(self, text="Reboot", command=self.clickRestartButton, font=("Helvetica", 18))
        self.restartButton.place(x=500, y=ym+ii*dym)
        self.mputEntry(self.h_start_edit,time.strftime("%Y-%m-%d_%H:%M:%S"))
        #

        ser=openSerial()
        print(ser)
        if ser is not None:
            with ser:
                ser.reset_input_buffer()
                ser.reset_output_buffer()

        if 1:
            self.scrolledText = ScrolledText(self, width=100, bd=10,
                                             relief="raised",font=("Helvetica", 10))
            self.scrolledText.place(x=700,y=70)
            #self.scrolledText.configure(state ='disabled')
            #
            self.startButton=tk.Button(self, text="Start", command=self.clickRun, font=("Helvetica", 18))
            self.startButton.place(x=700, y=10)
            self.task_is_running=0
            #

    '''def clickExitButton(self):
        return
        self.master.destroy()
    '''
    def run_task(self):
        delay=100
        ser = self.ser
        try:
            if ser.in_waiting>0:
                self.scrolledText.insert(tk.END,ser.read_all().decode('utf-8'))
                self.scrolledText.see(tk.END)
        except Exception as e:
            com=getComPort()
            if com:
                self.ser=serial.Serial(com,timeout=0.1)
            else:
                delay=1000

        if self.task_is_running:
            self.after(delay,self.run_task)
        else:
            self.ser.close()

    def clickRun(self):
        if self.startButton["text"]=="Start":
            print('Start')
            self.startButton.config(text="Stop")
            #
            self.ser=openSerial()
            if self.ser:
                self.ser.reset_input_buffer()
                self.ser.read_all()
                # start acquisition
                self.task_is_running = 1
                self.ser.write(b's\n')
                #
                self.after(100, self.run_task)
        else:
            print('Stop')
            self.startButton.config(text="Start")
            #
            if self.task_is_running==1:
                # stop acquisition
                self.ser.write(b'e\n')
                #
                line=self.ser.readline()
                if line:
                    txt=line.decode('utf-8')#.rstrip()
                    self.scrolledText.insert(tk.END,txt)
                    self.scrolledText.see(tk.END)
                self.task_is_running = 0
#
    def mEntry(self,txt,x,y,w,dx):
        label = tk.Label(self,text=txt,font=("Helvetica", 18))
        label.place(x=x-dx,y=y)
        edit = tk.Entry(self,text="", fg="Black", font=("Helvetica", 18),width=w)
        edit.place(x=x,y=y)
        return edit

    def mputEntry(self,edit,txt):
        edit.delete(0,tk.END)
        edit.insert(0,txt)

    def mgetEntry(self,ser,str,edit):
        data=str+edit.get()+"\n"
        ser.write(data.encode())
        ser.readline()

    def mgetParam(self,ser,txt):
        ser.write(txt.encode())
        txt=ser.readline().decode('utf-8').rstrip()
        ip=txt.find("=")
        return txt[ip+2:]

    def mUpdate(self,ser,edit,txt):
        txt1=self.mgetParam(ser,txt)
        self.mputEntry(edit,txt1)

    # following text is response to "p\n" command
    '''
    20:51:57.950 -> 
    20:51:57.950 -> ====================
    20:51:57.950 -> Adalogger_V2a
    20:51:57.950 -> Version    2.0.1
    20:51:57.950 -> unique_board_id: DF 64 70 A3 1B 7D 3C 2E 
    20:51:57.950 -> UID        1B7D3C2E
    20:51:57.950 -> eeprom (w) 255
    20:51:57.950 -> t_acq  (a) 60 sec
    20:51:57.950 -> t_on   (o) 1 min
    20:51:57.950 -> t_rep  (r) 0 min
    20:51:57.950 -> fsamp  (f) 96000 Hz
    20:51:57.950 -> again  (g) 20 dB
    20:51:57.950 -> Processing 0
    20:51:57.950 -> 255 255 255 255 255 255 255 255 255 255 255 255 255 255 255 255 
    '''
    def clickLoadButton(self):

        ser=openSerial()
        if ser:
            with ser:
                ser.reset_input_buffer()
                ser.read_all()
                #
                self.mUpdate(ser,self.mcuClocklabel,"?d")
                #
                self.mUpdate(ser,self.b_edit,"?n")
                self.mUpdate(ser,self.k_edit,"?k")
                self.mUpdate(ser,self.n_edit,"?l")
                #
                self.mUpdate(ser,self.t_acq_edit,"?a")
                self.mUpdate(ser,self.t_on_edit, "?o")
                self.mUpdate(ser,self.t_rep_edit,"?r")

                self.mUpdate(ser,self.fsamp_edit,"?f")
                self.mUpdate(ser,self.again_edit,"?g")
                #
                self.mUpdate(ser,self.sernum_edit,"?u")
                self.mUpdate(ser,self.proc_edit,"?p")
                #
                self.mUpdate(ser,self.h_1_edit,"?1")
                self.mUpdate(ser,self.h_2_edit,"?2")
                self.mUpdate(ser,self.h_3_edit,"?3")
                self.mUpdate(ser,self.h_4_edit,"?4")

    def clickSaveButton(self):
        #
        ser=openSerial()
        if ser:
            with ser:
                #        ser.read_all()
                self.mgetEntry(ser, '!n', self.b_edit)
                self.mgetEntry(ser, '!k', self.k_edit)
                self.mgetEntry(ser, '!l', self.n_edit)
                #
                self.mgetEntry(ser, '!a', self.t_acq_edit)
                self.mgetEntry(ser, '!o', self.t_on_edit)
                self.mgetEntry(ser, '!r', self.t_rep_edit)
                #
                self.mgetEntry(ser, '!f', self.fsamp_edit)
                self.mgetEntry(ser, '!g', self.again_edit)
                #
                self.mgetEntry(ser, '!1', self.h_1_edit)
                self.mgetEntry(ser, '!2', self.h_2_edit)
                self.mgetEntry(ser, '!3', self.h_3_edit)
                self.mgetEntry(ser, '!4', self.h_4_edit)
                #        #
                self.mgetEntry(ser, '!x', self.h_start_edit)
                ser.read_all()

    def clickRestartButton(self):
        print(self.h_start_edit)
        #
        ser=openSerial()
        if ser:
            ser.read_all()
            ser.write(b'b\n')  # seial line will be killed, so cannot close it (no with)

    def clickSyncButton(self):
        #
        ser=openSerial()
        if ser:
            ser.read_all()
            ser.write(b'c\n')
            print(ser.readline().decode('utf-8').rstrip())
            print(ser.readline().decode('utf-8').rstrip())
            #
            date_time = datetime.now()
            txt = date_time.strftime("%Y-%m-%d %H:%M:%S\n")
            #
            ser.write(txt.encode())
            print(ser.readline().decode('utf-8').rstrip())  # echo
            print(ser.readline().decode('utf-8').rstrip())  # date vector
            print(ser.readline().decode('utf-8').rstrip())  # text
            print(ser.readline().decode('utf-8').rstrip())  # new time

    def update_clock(self):
        now = time.strftime("%Y-%m-%d %H:%M:%S")
        self.pcClocklabel.configure(text=now)
        if self.timer_is_running:
            self.after(1000, self.update_clock)

#======================================================================================
class UploadFrame(ttk.Frame):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        tk.Label(self,text='Upload latest Firmware',font=_fnt).pack(pady=10)
        tk.Button(self, text="Fetch File from GitHub", command=self.do_fetch, font=_fnt).pack(pady=10)
        tk.Button(self, text="Setup MCU", command=self.do_mcuSetup, font=_fnt).pack(pady=10)
        tk.Button(self, text="Upload File", command=self.do_upload,font=_fnt).pack(pady=10)

        self.firmware='adalogger_V3.ino.uf2'

    def do_fetch(self):
        # get MCU type
        dev=getDevPid()
        if dev == None: return

        url0 = 'https://raw.githubusercontent.com/WMXZ-EU/LC-MARE/main/adalogger_V3/build/'

        if dev=='rp2040':
            url = url0 + 'rp2040.rp2040.adafruit_feather_adalogger/'+self.firmware
        if dev=='rp2350':
            url = url0+'rp2040.rp2040.adafruit_feather_rp2350_hstx/'+self.firmware

        with urlopen(url) as in_stream, open('adalogger_V3.ino.uf2', 'wb') as out_file:
            copyfileobj(in_stream, out_file)

    def do_mcuSetup(self):
        comPort = getComPort()
        if comPort==None: return
        #
        # set MCU into boot mode
        try:
           serial.Serial(comPort,1200)
        except serial.SerialException as e:
            #print(e)
            pass

    def do_upload(self):
        #
        for dsk in range(68, 100):
            if os.path.exists('%s:\\INFO_UF2.TXT' % chr(dsk)):
                break
        #
        disk=chr(dsk)
        dest = disk + "://NEW.UF2"
        #
        try:
            with open(self.firmware, mode='rb') as f:
                outbuf = f.read()

            with open(dest, "wb") as f:
                f.write(outbuf)
            print("Wrote %d bytes to %s" % (len(outbuf), dest))
        except:
            print('could not write to mcu (%s)' % dest)

#======================================================================================
class ConvertFrame(ttk.Frame):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        tk.Label(self, text='Convert *.bin to *.wav files',font=_fnt).pack(pady=10)
        tk.Button(self, text="Convert File", command=self.do_convert,font=_fnt).pack()
        tk.Button(self, text="Source Folder", command=self.do_source,font=_fnt).pack()
        tk.Button(self, text="Destination Folder", command=self.do_destination,font=_fnt).pack()
        self.btn_run=tk.Button(self, text="Convert Folder", command=self.do_batchConvert,font=_fnt)
        self.btn_run.pack()

        self.meta='meta'
        self.file_list=[]
        self.file_status={}
        self.file_result={}
        self._running=0

        self.out_folder = './'

        # File list
        self.flist = tk.LabelFrame(self, width=600, bd = 1,relief='solid',text='File list')
        self.flist.pack(fill="y", expand=True, side='bottom')
        self._build_file_list(self.flist)

    def do_source(self):
        folder = filedialog.askdirectory()
        if not folder:
            return
        print("Source Folder:",folder)
        paths = []
        for root, _, files in os.walk(folder):
            for f in sorted(files):
                if f.lower().endswith(".wav") or f.lower().endswith(".bin"):
                    paths.append(os.path.join(root, f))
        self._add_paths(paths)


    def _add_paths(self, paths, status='pending'):
        for p in paths:
            if p not in self.file_list:
                self.file_list.append(p)
                self.file_status[p] = status
                self.tree.insert("", "end", iid=p,
                                 values=(os.path.basename(p),status, "—"))

    def do_destination(self):
        self.out_folder = filedialog.askdirectory()
        if not self.out_folder:
            self.out_folder='./'
        print("Destination Folder:",self.out_folder)

        self.btn_run.config(text="▶  Start", foreground='Green')

    def do_convert(self,fname=None):
        if fname is None:
            fname = get_pamFileName()
        if fname == None: return
        dest = self.out_folder + '/' + basename(fname)[:-3] + 'wav'
        #
        try:
            hh, xx = loadData(fname)
            data, fs, nch, scale, it = convertData(hh, xx, fname)

            print(fname, 'fs=', fs, 'nch=', nch)
            if self.meta == 'meta':
                saveData(dest, hh, data)
            else:
                from scipy.io.wavfile import write
                write(dest, fs, data.astype('int32').reshape(-1, nch))

            result={fname: "done"}
            self.file_result[fname] = result
            self.after(0, lambda p=fname, r=result: self._file_done(p, r))
        except Exception as e:
            err = str(e)
            self.after(0, lambda p=fname, e=err: self._file_error(p, e))
            return 0
        return 1

    def do_batchConvert(self):
        if self._running==0:
            self.btn_run.config(text="⏹  Stop",foreground='Red')
            self._running=1
            threading.Thread(target=self._batch_thread, daemon=True).start()
        else:
            self.btn_run.config(text="▶  Start",foreground='Green')
            self._running=0
        return

    def _batch_thread(self):
        #
        for path in self.file_list:
            if self.file_status[path] == 'done':
                continue
            if self._running==0:
                break
            ret = self.do_convert(path)


    def _build_file_list(self, parent):
        # Treeview
        list_dict = {"name": ["File", 'w', 280],
                     "status": ["Status", 'center', 70]}

        cols = list_dict.keys()

        self.tree = ttk.Treeview(parent, columns=list(cols), show="headings",
                                  selectmode="extended")
        for key in cols:
            self.tree.heading(key, text=list_dict[key][0])
            self.tree.column(key,  width=list_dict[key][2], anchor=list_dict[key][1])

        #style = ttk.Style()
        #style.theme_use("default")
        #style.configure("Treeview",rowheight=22, font=("Helvetica", 12))
        #style.configure("Treeview.Heading",
        #                 font=("Helvetica", 12, "bold"), relief="flat")
        #style.map("Treeview", background=[("selected", 'lightgreen')],
        #                      foreground=[("selected", "black")])

        sb = ttk.Scrollbar(parent, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")

        self.tree.bind("<<TreeviewSelect>>", self._on_tree_select)

    def _on_tree_select(self, _event=None):
        sel = self.tree.selection()
        if not sel:
            return
        print(sel)
        path = sel[0]

        if path not in self.file_result:
            self.do_convert(path)

        self.tree.selection_remove(sel)
        return

    def _set_file_status(self, path, status):
        self.file_status[path] = status
        vals = self.tree.item(path, "values")
        self.tree.item(path, values=(vals[0], status))

    def _file_done(self, path, result):
        self.file_status[path] = 'done'
        self.tree.item(path,
                       values=(os.path.basename(path), 'done'))

    def _file_error(self, path, err):
        print(path,err)
        self.file_status[path] = 'error'
        self.tree.item(path, values=( os.path.basename(path), 'error'))

#========================================================================================
root = tk.Tk()
root.title("microPAM LC_Mare (WMXZ)")

s = ttk.Style()
s.configure('.', font=_fnt )
s.configure("Treeview.Heading",
                 font=("Helvetica", 12, "bold"), relief="flat")
s.map("Treeview", background=[("selected", 'lightgreen')],
                       foreground=[("selected", "black")])
#
notebook = ttk.Notebook()

notebook.add(WelcomeFrame(notebook,width=800,height=500),  text="Welcome ", padding=10)
notebook.add(   InfoFrame(notebook,width=600,height=500),  text="Info    ", padding=10)
notebook.add(   ViewFrame(notebook,width=500,height=500),  text="View    ", padding=10)
notebook.add(ConvertFrame(notebook,width=600,height=500),  text="Convert ", padding=10)
notebook.add( ConfigFrame(notebook,width=1500,height=500), text="Config  ", padding=10)
notebook.add(  UploadFrame(notebook,width=500,height=500), text="Upload  ", padding=10)

notebook.pack(padx=10, pady=10, fill=tk.BOTH)

def on_tab_changed(event):
    """Adjust window size to match the selected tab's frame."""
    selected_tab = event.widget.select()  # Get the tab ID
    frame = event.widget.nametowidget(selected_tab)  # Get the Frame widget
    # Get the requested size of the frame
    req_width = frame.winfo_reqwidth()
    req_height = frame.winfo_reqheight()
    if req_width<500: req_width = 500
    if req_height<200: req_height = 200
    # Add some padding for notebook tabs and borders
    extra_width = 25# notebook.winfo_width() - frame.winfo_width()
    extra_height = 100#  notebook.winfo_height() - frame.winfo_height()
    # Resize the main window
    root.geometry(f"{req_width + extra_width}x{req_height + extra_height}")
    #print(req_width, req_height, extra_width, extra_height)

# Bind tab change event
notebook.bind("<<NotebookTabChanged>>", on_tab_changed)

from tkinter import messagebox

def on_closing():
    if messagebox.askokcancel("Quit", "Do you want to quit?"):
        root.quit() # needed for pycharm to stop
        root.destroy()

# Handle the window close event (X button)
root.protocol("WM_DELETE_WINDOW", on_closing)

# Set the top_window on top of the root window
root.wm_attributes("-topmost", True)

root.mainloop()
