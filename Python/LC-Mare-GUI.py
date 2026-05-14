# uncomment prev line and remove ' ' between % % save cell to file (is only in LC-Mare-GUI.ipynb file)
# LC-Mare-GUI
# use this cell to test and develop GUI
# to compile "pyinstaller LC-Mare-GUI.py --noconfirm"
# will generate "dist/LC-Mare-GUI/LC-Mare-GUI.exe"
# and  "dist/LC-Mare-GUI/_internal" with all required pyd/dll files
#

import os
import tkinter as tk
from tkinter.scrolledtext import ScrolledText
import time
from datetime import datetime
import serial
import serial.tools.list_ports

def getComPort():
    s=serial.tools.list_ports.comports(True)
    print(s)
    for ii in range(len(s)):
        if (s[ii].vid==0x239a):
            if (s[ii].pid==0x815d) | (s[ii].pid==0x814f): # adafruit adalogger rp2040 or feather rp2350
                return s[ii].device
    return None

class Window(tk.Frame):
    def __init__(self, master=None):
        tk.Frame.__init__(self, master)        
        self.master = master

        # widget can take all window
        self.pack(fill=tk.BOTH, expand=1)

        label1 = tk.Label(text="PC:",font=("Helvetica", 18))
        label1.place(x=250,y=10)
        self.pcClocklabel = tk.Label(text="", fg="Red", font=("Helvetica", 18))
        self.pcClocklabel.place(x=320,y=10)
        self.update_clock()

        label2 = tk.Label(text="MCU:",font=("Helvetica", 18))
        label2.place(x=240,y=50)
        self.mcuClocklabel = tk.Label(text="", fg="Black", font=("Helvetica", 18))
        self.mcuClocklabel.place(x=320,y=50)

        xo=100
        yo=50
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
        self.h_start_edit = self.mEntry("h_start (h):",xo-320,yo+ii*40,3,130);

        # temporary disabling input
        self.shift_edit.configure(state="disabled")
        # end disabling input

        # create buttons
        xm=600
        ym=170
        dym=60
        ii=0
        tk.Button(self, text="Exit", command=self.clickExitButton, font=("Helvetica", 18)).place(x=xm, y=10)
        tk.Button(self, text="Load", command=self.clickLoadButton, font=("Helvetica", 18)).place(x=xm, y=ym+ii*dym); ii+=1
        tk.Button(self, text="Sync", command=self.clickSyncButton, font=("Helvetica", 18)).place(x=xm, y=ym+ii*dym); ii+=1
        tk.Button(self, text="Save", command=self.clickSaveButton, font=("Helvetica", 18)).place(x=xm, y=ym+ii*dym); ii+=1

        self.restartButton = tk.Button(self, text="Reboot", command=self.clickRestartButton, font=("Helvetica", 18))
        self.restartButton.place(x=300, y=ym+ii*dym)
        self.mputEntry(self.h_start_edit,'0')
        #

        s=serial.tools.list_ports.comports(True)
        if len(s)>0:
            with serial.Serial(s[0].device) as ser:
                ser.reset_input_buffer()
                ser.reset_output_buffer()

        if 1:
            self.scrolledText = ScrolledText(self.master, width=100, bd=10, 
                                             relief="raised",font=("Helvetica", 10))
            self.scrolledText.place(x=700,y=70)
            #self.scrolledText.configure(state ='disabled')
            #
            self.startButton=tk.Button(self, text="Start", command=self.clickRun, font=("Helvetica", 18))
            self.startButton.place(x=700, y=10)
            self.task_is_running=0
            #
            self.monitorButton=tk.Button(self, text="Monitor", command=self.clickMonitor, font=("Helvetica", 18))
            self.monitorButton.place(x=800, y=10)
            self.monitor_is_running=0

    def clickExitButton(self):
        self.master.destroy() 

    def run_task(self):
        ser = self.ser
        if ser.in_waiting>0:
            self.scrolledText.insert(tk.END,ser.read_all().decode('utf-8'))
            self.scrolledText.see(tk.END)
        if self.task_is_running:
            self.after(100,self.run_task)

    def clickRun(self):
        if self.startButton["text"]=="Start":
            print('Start')
            self.startButton.config(text="Stop")
            #
            com=getComPort()
            if com:
                print('start',com)
                self.ser=serial.Serial(com,timeout=0.1)
                if self.ser:
                    self.ser.reset_input_buffer()
                    self.ser.read_all()
                    # start acquisition
                    self.task_is_running=1
                    self.ser.write(b's\n')
                    #
                    self.after(100,self.run_task)
        else:
            print('Stop')
            self.startButton.config(text="Start")
            #
            if self.task_is_running==1:
                # stop acquisition
                self.task_is_running=0
                self.ser.write(b'e\n')
                #
                line=self.ser.readline()
                if line:
                    txt=line.decode('utf-8')#.rstrip()
                    self.scrolledText.insert(tk.END,txt)
                    self.scrolledText.see(tk.END)
                self.ser.close()

    def monitor_task(self):
        ser = self.ser
        if ser.in_waiting>0:
            self.scrolledText.insert(tk.END,ser.read_all().decode('utf-8'))
            self.scrolledText.see(tk.END)
        if self.monitor_is_running:
            self.after(100,self.monitor_task)

    def clickMonitor(self):
        if self.monitor_is_running==1:
            self.monitor_is_running=0
        else:
            self.monitor_is_running=1
            com=getComPort()
            if com:
                self.ser=serial.Serial(com,timeout=0.1)
                if self.ser:
                    self.after(100,self.monitor_task)

    def mEntry(self,txt,x,y,w,dx):
        label = tk.Label(text=txt,font=("Helvetica", 18))
        label.place(x=x-dx,y=y)
        edit = tk.Entry(text="", fg="Black", font=("Helvetica", 18),width=w)
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

        com=getComPort()
        if com:
            print('load',com)
            with serial.Serial(com) as ser:
                ser.reset_input_buffer()
                ser.read_all()
                #
                # load now data from device
                ser.write(b'c\n')
                txt1=ser.readline().decode('utf-8').rstrip()
                ser.readline().decode('utf-8').rstrip()
                #
                ser.write("\n".encode())
                ser.readline().decode('utf-8').rstrip() # empty echo
                ser.readline().decode('utf-8').rstrip() # text

                #ser.write(b'?d\n')
                #txt1=ser.readline().decode('utf-8').rstrip()
                ip1=txt1.find("rtc")
                self.mcuClocklabel.configure(text=txt1[ip1+4:])
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
        com=getComPort()
        if com:
            print('save',com)
            with serial.Serial(com) as ser:
        #        ser.read_all()
                self.mgetEntry(ser,'!n',self.b_edit)
                self.mgetEntry(ser,'!k',self.k_edit)
                self.mgetEntry(ser,'!l',self.n_edit)
                #
                self.mgetEntry(ser,'!a',self.t_acq_edit)
                self.mgetEntry(ser,'!o',self.t_on_edit)
                self.mgetEntry(ser,'!r',self.t_rep_edit)
                #
                self.mgetEntry(ser,'!f',self.fsamp_edit)
                self.mgetEntry(ser,'!g',self.again_edit)
                #
                self.mgetEntry(ser,'!1',self.h_1_edit)
                self.mgetEntry(ser,'!2',self.h_2_edit)
                self.mgetEntry(ser,'!3',self.h_3_edit)
                self.mgetEntry(ser,'!4',self.h_4_edit)
        #        #
                ser.read_all()

    def clickRestartButton(self):
        have_delay=self.h_start_edit.get()=='0'
        print(have_delay)
        #
        com=getComPort()
        if com:
            print('restart',com)
            with serial.Serial(com) as ser:
                ser.read_all()
                if have_delay:
                    ser.write("x\n".encode())
                else:
                    self.mgetEntry(ser,'x',self.h_start_edit)

    def clickSyncButton(self):
        #
        com=getComPort()
        if com:
            print('sync',com)
            with serial.Serial(com,timeout=1.0) as ser:
                ser.read_all()
                ser.write(b'c\n')
                print(ser.readline().decode('utf-8').rstrip())
                print(ser.readline().decode('utf-8').rstrip())
                #
                date_time=datetime.now()
                txt=date_time.strftime("%Y-%m-%d %H:%M:%S\n")
                #
                ser.write(txt.encode())
                print(ser.readline().decode('utf-8').rstrip()) # echo
                print(ser.readline().decode('utf-8').rstrip()) # date vector
                print(ser.readline().decode('utf-8').rstrip()) # text
                print(ser.readline().decode('utf-8').rstrip()) # new time


    def update_clock(self):
        now = time.strftime("%Y-%m-%d %H:%M:%S")
        self.pcClocklabel.configure(text=now)
        self.after(1000, self.update_clock)

root = tk.Tk()
app = Window(root)
root.wm_title("MicroPAM LC-Mare (WMXZ)")
if 0:
    root.geometry("700x500")
else:
    root.geometry("1500x500")

root.after(1000, app.update_clock)
root.mainloop()
