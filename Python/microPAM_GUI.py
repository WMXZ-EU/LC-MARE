# uncomment prev line to save cell to file
# micoPAM GUI
# use this cell to test and develop GUI
# to compile "pyinstaller microPAM_GUI.py --noconfirm"
# will generate "dist/micoPAM_GUI/microPAM_GUI.exe"
# and  "dist/micoPAM_GUI/_internal" with all required pyd/dll files
#
import tkinter as tk
import time
from datetime import datetime
import serial
import serial.tools.list_ports

class Window(tk.Frame):

    def mEntry(self,txt,x,y,w,dx):
        label = tk.Label(text=txt,font=("Helvetica", 18))
        label.place(x=x-dx,y=y)
        edit = tk.Entry(text="", fg="Black", font=("Helvetica", 18),width=w)
        edit.place(x=x,y=y)
        return edit

    def mgetParam(self,ser,txt):
        ser.write(txt.encode())
        txt=ser.readline().decode('utf-8').rstrip()
        ip=txt.find("=")
        return txt[ip+2:]

    def mputEntry(self,edit,txt):
        edit.delete(0,tk.END)
        edit.insert(0,txt)

    def mUpdate(self,ser,edit,txt):
        ser.write(txt.encode())
        txt=ser.readline().decode('utf-8').rstrip()
        ip=txt.find("=")
        edit.delete(0,tk.END)
        edit.insert(0,txt[ip+2:])

    def mgetEntry(self,ser,str,edit):
        data=str+edit.get()+"\r"
        ser.write(data.encode())

    def ndays(self,d,m,y):
        def lpY(y): return (y%4==0) | ((y%100==0) & (y%400>0))
        dom=[31,28,31,30,31,30,31,31,30,31,30]
        #
        # number of days since 1-1-1970
        y1=y-1970
        days=y1*365
        for ii in range(y1): 
            if lpY(1970+ii): days +=1 
        #
        m -= 1
        for ii in range(m):
            days += dom[ii]
            if ii==1:
                if lpY(y): days += 1
        #
        d -= 1
        days += d
        return days, (days+4)%7 # 1-1-70 was thursday 1-1-24 was monday
    
    def nidays(self,days):
        def lpY(y): return (y%4==0) | ((y%100==0) & (y%400>0))
        dom=[31,28,31,30,31,30,31,31,30,31,30]
        #
        y1=0
        while days>0:
            if lpY(1970+y1): 
                days -=366
            else:
                days -= 365
            y1 +=1
        #
        if y1>0:
            y1 -= 1
        if days<=0:
            if lpY(1970+y1): 
                days += 366
            else:
                days += 365
        #
        days += 1
        ii = 0
        while days >=0:
            if (ii==1) & lpY(1970+y1): days -=1
            days -= dom[ii]
            ii += 1
        ii=ii-1
        days += dom[ii]
        m = ii
        return (1970+y1,m+1,days)

    def __init__(self, master=None):
        tk.Frame.__init__(self, master)        
        self.master = master

        # widget can take all window
        self.pack(fill=tk.BOTH, expand=1)

        label1 = tk.Label(text="PC:",font=("Helvetica", 18))
        label1.place(x=100,y=10)
        self.pcClocklabel = tk.Label(text="", fg="Red", font=("Helvetica", 18))
        self.pcClocklabel.place(x=160,y=10)
        self.update_clock()

        label2 = tk.Label(text="MCU:",font=("Helvetica", 18))
        label2.place(x=80,y=60)
        self.mcuClocklabel = tk.Label(text="", fg="Black", font=("Helvetica", 18))
        self.mcuClocklabel.place(x=160,y=60)

        xo=120
        yo=110
        ii=0
        self.fsamp_edit = self.mEntry("fsamp:",xo,yo+ii*40,10,80); ii+=1
        self.proc_edit  = self.mEntry("proc:",xo,yo+ii*40,5,80); ii+=1
        self.shift_edit = self.mEntry("shift:",xo,yo+ii*40,5,80); ii+=1
        self.again_edit = self.mEntry("again:",xo,yo+ii*40,5,80); ii+=1

        xo=350
        yo=110
        ii=0
        self.t_acq_edit = self.mEntry("t_acq:",xo,yo+ii*40,5,80); ii+=1
        self.t_on_edit  = self.mEntry("t_on:",xo,yo+ii*40,5,80); ii+=1
        self.t_rep_edit = self.mEntry("t_rep:",xo,yo+ii*40,5,80); ii+=1
        ii=0
        xo += 160
        self.h_1_edit = self.mEntry("h_1:",xo,yo+ii*40,5,80); ii+=1
        self.h_2_edit = self.mEntry("h_2:",xo,yo+ii*40,5,80); ii+=1
        self.h_3_edit = self.mEntry("h_3:",xo,yo+ii*40,5,80); ii+=1
        self.h_4_edit = self.mEntry("h_4:",xo,yo+ii*40,5,80); ii+=1
        yo += 40
        self.d_start_edit = self.mEntry("d_start:",xo-320,yo+ii*40,3,90); 
        self.m_start_edit = self.mEntry("m_start:",xo-160,yo+ii*40,3,90); 
        self.y_start_edit = self.mEntry("y_start:",xo,yo+ii*40,5,90); ii+=1
        self.d_on_edit    = self.mEntry("d_on:",xo,yo+ii*40,5,80); ii+=1
        self.d_rep_edit   = self.mEntry("d_rep:",xo,yo+ii*40,5,80); ii+=1

        # create buttons
        xm=600
        exitButton = tk.Button(self, text="Exit ", command=self.clickExitButton, font=("Helvetica", 18))
        exitButton.place(x=xm, y=10)
        loadButton = tk.Button(self, text="Load", command=self.clickLoadButton, font=("Helvetica", 18))
        loadButton.place(x=xm, y=80)
        saveButton = tk.Button(self, text="Save", command=self.clickSaveButton, font=("Helvetica", 18))
        saveButton.place(x=xm, y=150)
        storeButton = tk.Button(self, text="Store", command=self.clickStoreButton, font=("Helvetica", 18))
        storeButton.place(x=xm, y=220)
        syncButton = tk.Button(self, text="Sync", command=self.clickSyncButton, font=("Helvetica", 18))
        syncButton.place(x=xm, y=310)
        self.storeButton=storeButton
        #
        date_time=datetime.now()
        self.mputEntry(self.d_start_edit,str(date_time.day))
        self.mputEntry(self.m_start_edit,str(date_time.month))
        self.mputEntry(self.y_start_edit,str(date_time.year))

        s=serial.tools.list_ports.comports(True)
        with serial.Serial(s[0].device) as ser:
            ser.reset_input_buffer()
            ser.reset_output_buffer()


    def clickExitButton(self):
        self.master.destroy() 

    def clickLoadButton(self):
        s=serial.tools.list_ports.comports(True)
        if len(s)==0:
            return
        with serial.Serial(s[0].device) as ser:
            ser.reset_input_buffer()
            # stop acquisition
            ser.write(b'e\r')
            txt=ser.readline().decode('utf-8').rstrip()
            # stop monitor
            ser.write(b':m0\r')
            txt=ser.readline().decode('utf-8').rstrip()
            ser.reset_input_buffer()
            #
            # load now data from device
            ser.write(b'?d\r')
            txt1=ser.readline().decode('utf-8').rstrip()
            ser.write(b'?t\r')
            txt2=ser.readline().decode('utf-8').rstrip()
            ip1=txt1.find("=")
            ip2=txt2.find("=")
            self.mcuClocklabel.configure(text=txt1[ip1+2:]+txt2[ip2+1:])
            #
            self.mUpdate(ser,self.t_acq_edit,"?a")
            self.mUpdate(ser,self.t_on_edit, "?o")
            self.mUpdate(ser,self.t_rep_edit,"?r")
            #
            self.mUpdate(ser,self.h_1_edit,"?1")
            self.mUpdate(ser,self.h_2_edit,"?2")
            self.mUpdate(ser,self.h_3_edit,"?3")
            self.mUpdate(ser,self.h_4_edit,"?4")
            #
            self.mUpdate(ser,self.d_on_edit,"?5")
            self.mUpdate(ser,self.d_rep_edit,"?6")
            #
            self.mUpdate(ser,self.fsamp_edit,"?f")
            self.mUpdate(ser,self.proc_edit, "?c")
            self.mUpdate(ser,self.shift_edit,"?s")
            self.mUpdate(ser,self.again_edit,"?g")

            days=int(self.mgetParam(ser,"?0"))
            print('get',days)
            year,month,day=self.nidays(days+20000)
            self.mputEntry(self.d_start_edit,str(day))
            self.mputEntry(self.m_start_edit,str(month))
            self.mputEntry(self.y_start_edit,str(year))
        self.storeButton["state"]=tk.DISABLED

    def clickSaveButton(self):
        dx=self.d_start_edit.get()
        mx=self.m_start_edit.get()
        yx=self.y_start_edit.get()
        days,dow=self.ndays(int(dx),int(mx),int(yx))
        #
        s=serial.tools.list_ports.comports(True)
        with serial.Serial(s[0].device) as ser:
            ser.read_all()
            self.mgetEntry(ser,'!a',self.t_acq_edit)
            self.mgetEntry(ser,'!o',self.t_on_edit)
            self.mgetEntry(ser,'!r',self.t_rep_edit)
            self.mgetEntry(ser,'!1',self.h_1_edit)
            self.mgetEntry(ser,'!2',self.h_2_edit)
            self.mgetEntry(ser,'!3',self.h_3_edit)
            self.mgetEntry(ser,'!4',self.h_4_edit)
            self.mgetEntry(ser,'!5',self.d_on_edit)
            self.mgetEntry(ser,'!6',self.d_rep_edit)
            #
            self.mgetEntry(ser,'!f',self.fsamp_edit)
            self.mgetEntry(ser,'!c',self.proc_edit)
            self.mgetEntry(ser,'!s',self.shift_edit)
            self.mgetEntry(ser,'!g',self.again_edit)
            #
            data="!0"+str(days-20000)+"\r"
            print('put', data)
            ser.write(data.encode())
            ser.read_all()
        self.storeButton["state"]=tk.NORMAL

    def clickStoreButton(self):
        s=serial.tools.list_ports.comports(True)
        with serial.Serial(s[0].device) as ser:
            ser.read_all()
            ser.write("!w1\r".encode())
            time.sleep(0.1)
            txt=ser.readline().decode('utf-8').rstrip()
            #
            ser.write(":w".encode())
            time.sleep(0.1)
            txt=ser.readline().decode('utf-8').rstrip()
            txt=ser.readline().decode('utf-8').rstrip()
            print(txt)

    def clickSyncButton(self):
        s=serial.tools.list_ports.comports(True)
        date_time=datetime.now()
        dd=date_time.strftime("!d%Y-%m-%d\r")
        tt=date_time.strftime("!t%H:%M:%S\r")

        with serial.Serial(s[0].device) as ser:
            ser.write(tt.encode())
        with serial.Serial(s[0].device) as ser:
            ser.write(dd.encode())
        with serial.Serial(s[0].device) as ser:
            ser.write(b':c')

    def update_clock(self):
        now = time.strftime("%Y-%m-%d %H:%M:%S")
        self.pcClocklabel.configure(text=now)
        self.after(1000, self.update_clock)

root = tk.Tk()
app = Window(root)
root.wm_title("MicroPAM V3 (WMXZ)")
root.geometry("700x500")

microPAM_connected = 1      # change to 1 if microPAM is connected to USB
if microPAM_connected:
    root.after(1000, app.update_clock)
    root.mainloop() 
