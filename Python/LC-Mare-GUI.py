# uncomment prev line to save cell to file (is only in LC-Mare-UI.ipynb file)
# LC-Mare-GUI
# use this cell to test and develop GUI
# to compile "pyinstaller LC-Mare-GUI.py --noconfirm"
# will generate "dist/LC-Mare-GUI/LC-Mare-GUI.exe"
# and  "dist/LC-Mare-GUI/_internal" with all required pyd/dll files
#
import os
import tkinter as tk
import time
from datetime import datetime
import serial
import serial.tools.list_ports

def getComPort():
    s=serial.tools.list_ports.comports(True)
    for ii in range(len(s)):
        if (s[ii].pid==0x815d) & (s[ii].vid==0x239a): # adafruit adalogger rp2040
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

        xo=120
        yo=170
        ii=0
        self.fsamp_edit = self.mEntry("fsamp:",xo,yo+ii*40,6,80); ii+=1
        self.proc_edit  = self.mEntry("proc:", xo,yo+ii*40,1,80); ii+=1
        self.shift_edit = self.mEntry("shift:",xo,yo+ii*40,2,80); ii+=1
        self.again_edit = self.mEntry("again:",xo,yo+ii*40,2,80); ii+=1

        xo=350
        yo=170
        ii=0
        self.t_acq_edit = self.mEntry("t_acq:",xo,yo+ii*40,5,80); ii+=1
        self.t_on_edit  = self.mEntry("t_on:", xo,yo+ii*40,5,80); ii+=1
        self.t_rep_edit = self.mEntry("t_rep:",xo,yo+ii*40,5,80); ii+=1
        ii=0
        xo += 160
        self.h_1_edit = self.mEntry("h_1:",xo,yo+ii*40,2,60); ii+=1
        self.h_2_edit = self.mEntry("h_2:",xo,yo+ii*40,2,60); ii+=1
        self.h_3_edit = self.mEntry("h_3:",xo,yo+ii*40,2,60); ii+=1
        self.h_4_edit = self.mEntry("h_4:",xo,yo+ii*40,2,60); ii+=1
        yo += 30
        self.d_start_edit = self.mEntry("d_start:",xo-320,yo+ii*40,3,90); 
        self.m_start_edit = self.mEntry("m_start:",xo-160,yo+ii*40,3,90); 
        self.y_start_edit = self.mEntry("y_start:",xo,yo+ii*40,5,90); ii+=1
        #
        self.d_on_edit    = self.mEntry("d_on:", xo,yo+ii*40,5,80); ii+=1
        self.d_rep_edit   = self.mEntry("d_rep:",xo,yo+ii*40,5,80); ii+=1

        # temporary disabling input
        #self.b_edit.configure(state="disabled")
        #self.k_edit.configure(state="disabled")
        #self.n_edit.configure(state="disabled")

        self.shift_edit.configure(state="disabled")

        self.h_1_edit.configure(state="disabled")
        self.h_2_edit.configure(state="disabled")
        self.h_3_edit.configure(state="disabled")
        self.h_4_edit.configure(state="disabled")

        self.d_start_edit.configure(state="disabled")
        self.m_start_edit.configure(state="disabled")
        self.y_start_edit.configure(state="disabled")
        self.d_on_edit.configure(state="disabled")
        self.d_rep_edit.configure(state="disabled")
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
        self.storeButton = tk.Button(self, text="Store", command=self.clickStoreButton, font=("Helvetica", 18))
        self.storeButton.place(x=xm, y=ym+ii*dym)

        #
        date_time=datetime.now()
        self.mputEntry(self.d_start_edit,str(date_time.day))
        self.mputEntry(self.m_start_edit,str(date_time.month))
        self.mputEntry(self.y_start_edit,str(date_time.year))

        com=getComPort()
        if com:
            print('init',com)
            with serial.Serial(com) as ser:
                ser.reset_input_buffer()
                ser.reset_output_buffer()

    def clickExitButton(self):
        self.master.destroy() 

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
        data=str+edit.get()+"\r"
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

    '''    
    def ndays(self,d,m,y):
        def lpY(y): return (y%4==0) | ((y%100==0) & (y%400>0))
        dom=[31,28,31,30,31,30,31,31,30,31,30,31]
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
        dom=[31,28,31,30,31,30,31,31,30,31,30,31]
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
        y1 += 1970
        if days<=0:
            if lpY(y1): 
                days += 366
            else:
                days += 365
        #
        days += 1
        m = 0
        while days >=0:
            if (m==1) & lpY(y1): days -=1
            days -= dom[m]
            m += 1
        days += dom[m-1]
        return (y1,m,days)

    '''

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
        self.storeButton["state"]=tk.DISABLED

        com=getComPort()
        if com:
            print('load',com)
            with serial.Serial(com) as ser:
                ser.reset_input_buffer()
                ser.read_all()
                # stop acquisition
                #ser.write(b'e\r')
                #txt=ser.readline().decode('utf-8').rstrip()
                #print(txt)
                '''
                ser.write(b'p\r')
                txt1=ser.readline().decode('utf-8').rstrip()
                txt1=ser.readline().decode('utf-8').rstrip() # ====================
                txt1=ser.readline().decode('utf-8').rstrip() # Adalogger_V2a
                txt2=ser.readline().decode('utf-8').rstrip() #  Version    2.0.x
                txt1=ser.readline().decode('utf-8').rstrip() #  unique_board_id: DF 64 3C F0 13 5B 23 26 
                txt1=ser.readline().decode('utf-8').rstrip() #  UID        135B2326
                #print(txt1)
                if txt1[:3]=='UID': self.mputEntry(self.sernum_edit,txt1[11:])
                txt1=ser.readline().decode('utf-8').rstrip() #  eeprom (w) 255
                #print('3',txt1)
                #if txt1[9]=='w': print(txt1[11:])
                txt1=ser.readline().decode('utf-8').rstrip() #  t_acq  (a) 60 sec
                #print('4',txt1)
                #if txt1[9]=='a': print(txt1[11:])
                txt1=ser.readline().decode('utf-8').rstrip() #  t_on   (o) 1 min
                #print('5',txt1)
                #if txt1[9]=='o': print(txt1[11:])
                txt1=ser.readline().decode('utf-8').rstrip() #  t_rep  (r) 0 min
                #print('6',txt1)
                #if txt1[9]=='r': print(txt1[11:])
                txt1=ser.readline().decode('utf-8').rstrip() #  fsamp  (f) 192000 Hz
                #print('7',txt1)
                #if txt1[9]=='f': print(txt1[11:])
                txt1=ser.readline().decode('utf-8').rstrip() #  again  (g) 0 dB
                #print('8',txt1)
                #if txt1[9]=='g': print(txt1[11:])
                txt1=ser.readline().decode('utf-8').rstrip() #  Processing 0
                #print('9',txt1)
                if txt1[0]=='P': self.mputEntry(self.proc_edit,txt1[11:])
                txt1=ser.readline().decode('utf-8').rstrip() #  eprom content
                print('10',txt1)
                '''
                ## stop monitor
                #ser.write(b':m0\r')
                #txt=ser.readline().decode('utf-8').rstrip()
                #ser.reset_input_buffer()
                #
                # load now data from device
                ser.write(b'?d\r')
                txt1=ser.readline().decode('utf-8').rstrip()
                ip1=txt1.find("=")
                self.mcuClocklabel.configure(text=txt1[ip1+2:])
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
                #days=int(self.mgetParam(ser,"?0"))
                #year,month,day=self.nidays(days+20000)
                #self.mputEntry(self.d_start_edit,str(day))
                #self.mputEntry(self.m_start_edit,str(month))
                #self.mputEntry(self.y_start_edit,str(year))
        #else:
        #    #for items in os.listdir():  print(items)
        #    print("current directory: ",os.getcwd())
        #    with open("config.txt","r") as f:
        #        for line in f:
        #            ip0=line.find("=")
        #            ip1=line.find(";")
        #            match line[0]:
        #                case 'b': self.mputEntry(self.b_edit,line[ip0+1:ip1])
        #                case 'k': self.mputEntry(self.k_edit,line[ip0+1:ip1])
        #                case 'n': self.mputEntry(self.n_edit,line[ip0+1:ip1])
        #
        #                case 'a': self.mputEntry(self.t_acq_edit,line[ip0+1:ip1])
        #                case 'o': self.mputEntry(self.t_on_edit, line[ip0+1:ip1])
        #                case 'r': self.mputEntry(self.t_rep_edit,line[ip0+1:ip1])
        #                #
        #                case '1': self.mputEntry(self.h_1_edit,line[ip0+1:ip1])
        #                case '2': self.mputEntry(self.h_2_edit,line[ip0+1:ip1])
        #                case '3': self.mputEntry(self.h_3_edit,line[ip0+1:ip1])
        #                case '4': self.mputEntry(self.h_4_edit,line[ip0+1:ip1])
        #                #
        #                case '5': self.mputEntry(self.d_on_edit, line[ip0+1:ip1])
        #                case '6': self.mputEntry(self.d_rep_edit,line[ip0+1:ip1])
        #                #
        #                case 'f': self.mputEntry(self.fsamp_edit,line[ip0+1:ip1])
        #                case 'c': self.mputEntry(self.proc_edit, line[ip0+1:ip1])
        #                case 's': self.mputEntry(self.shift_edit,line[ip0+1:ip1])
        #                case 'g': self.mputEntry(self.again_edit,line[ip0+1:ip1])
        #                #
        #                case '0':
        #                    days=int(line[ip0+1:ip1])
        #                    year,month,day=self.nidays(days+20000)
        #                    self.mputEntry(self.d_start_edit,str(day))
        #                    self.mputEntry(self.m_start_edit,str(month))
        #                    self.mputEntry(self.y_start_edit,str(year))


    def clickSaveButton(self):
        #dx=self.d_start_edit.get()
        #mx=self.m_start_edit.get()
        #yx=self.y_start_edit.get()
        #days,dow=self.ndays(int(dx),int(mx),int(yx))
        ##
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
        #        self.mgetEntry(ser,'!1',self.h_1_edit)
        #        self.mgetEntry(ser,'!2',self.h_2_edit)
        #        self.mgetEntry(ser,'!3',self.h_3_edit)
        #        self.mgetEntry(ser,'!4',self.h_4_edit)
        #        #
        #        self.mgetEntry(ser,'!5',self.d_on_edit)
        #        self.mgetEntry(ser,'!6',self.d_rep_edit)
                #
                self.mgetEntry(ser,'!f',self.fsamp_edit)
                self.mgetEntry(ser,'!g',self.again_edit)
        #        self.mgetEntry(ser,'!p',self.proc_edit)
        #        self.mgetEntry(ser,'!s',self.shift_edit)
        #        #
        #        data="!0"+str(days-20000)+"\r"
        #        print('put', data)
        #        ser.write(data.encode())
        #        #
                ser.read_all()
        #with open("config.txt","w") as f:
        #    f.write("b="+self.b_edit.get()+"; author\n")
        #    f.write("k="+self.k_edit.get()+"; project\n")
        #    f.write("n="+self.n_edit.get()+"; site\n")
        #    #
        #    f.write("a="+self.t_acq_edit.get()+"; t_acq\n")
        #    f.write("o="+self.t_on_edit.get()+"; t_on\n")
        #    f.write("r="+self.t_rep_edit.get()+"; t_rep\n")
        #    #
        #    f.write("1="+self.h_1_edit.get()+"; h_1\n")
        #    f.write("2="+self.h_2_edit.get()+"; h_2\n")
        #    f.write("3="+self.h_3_edit.get()+"; h_3\n")
        #    f.write("4="+self.h_4_edit.get()+"; h_4\n")
        #    #
        #    f.write("5="+self.d_on_edit.get()+"; d_on\n")
        #    f.write("6="+self.d_rep_edit.get()+"; d_rep\n")
        #    #
        #    f.write("f="+self.fsamp_edit.get()+"; fsamp\n")
        #    f.write("c="+self.proc_edit.get()+"; proc\n")
        #    f.write("s="+self.shift_edit.get()+"; shift\n")
        #    f.write("g="+self.again_edit.get()+"; again\n")
        #    #
        #    f.write("0="+str(days-20000)+"; d_0\n")
        #
        #print("current directory: ",os.getcwd())
        ##for items in os.listdir():  print(items)
        #self.storeButton["state"]=tk.NORMAL

    def clickStoreButton(self):
        com=getComPort()
        if com:
            print('store',com)
            with serial.Serial(com) as ser:
                ser.read_all()
        #        ser.write("!w1\r".encode())
        #        time.sleep(0.1)
        #        txt=ser.readline().decode('utf-8').rstrip()
        #        #
        #        ser.write(":w".encode())
        #        time.sleep(0.1)
        #        txt=ser.readline().decode('utf-8').rstrip()
        #        txt=ser.readline().decode('utf-8').rstrip()
        #        print(txt)

    def clickSyncButton(self):
        date_time=datetime.now()
        date_string=date_time.strftime("!d%Y-%m-%d %H:%M:%S\r")
        #
        com=getComPort()
        if com:
            print('sync',com)
            with serial.Serial(com) as ser:
                ser.write(date_string.encode())
                ser.readline()
                ser.readline()

    def update_clock(self):
        now = time.strftime("%Y-%m-%d %H:%M:%S")
        self.pcClocklabel.configure(text=now)
        self.after(1000, self.update_clock)

root = tk.Tk()
app = Window(root)
root.wm_title("MicroPAM LC-Mare (WMXZ)")
root.geometry("700x500")

root.after(1000, app.update_clock)
root.mainloop()
