# uncomment prev line and remove ' ' between % % save cell to file (is only in microPAM_Viewer.ipynb file)
#
# microPAM Viewer (WMXZ) 24-03-2026
# inspired by Nauta scientific (M.M.)
# modified to generate psd, median and ACI plots
#
# to compile "pyinstaller microPAM_Viewer.py --noconfirm"
# will generate "dist/micoPAM_Viewer/microPAM_Viewer.exe"
# and  "dist/micoPAM_Viewer/_internal" with all required pyd/dll files
#=============================================================================
import sys
import os
import numpy as np

import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import (FigureCanvasTkAgg, NavigationToolbar2Tk)

import tkinter as tk
from tkinter import ttk, filedialog, messagebox

import threading

from microPAM import load_microPAM,dB

def load_wav(filepath):
    return load_microPAM(filepath)

def spectrogram(xx, fs, nfft=1024,nw=512,ns=256):
    nx=xx.shape[0]
    nfr=1+nfft//2
    offsets=np.arange(0,nx-nw,ns)
    ni=len(offsets)
    X=np.zeros((nfr,ni),dtype='complex')

    W=np.hanning(nw)
    for ii,j1 in enumerate(offsets):
        j2=min(nx,j1+nw)
        X[:,ii]= np.fft.rfft(xx[j1:j2]*W,nfft)
    F=np.arange(nfr)*fs/nfft
    T=(ns+offsets)/fs
    return F,T,X

def analysis(fq,tq,qq,wa,fmin=0,fmax=None):
    nf,nq=qq.shape
    ns=int(nq/(tq[-1]-tq[0])*wa)
    ni=np.arange(0,nq,ns)
    nd=len(ni)

    if fmax==None:
        fmax=fq[-1]
    ifr= np.where((fq>=fmin) & (fq <=fmax))[0]
    F=fq[ifr]
    #
    T=np.zeros(nd)
    Q=np.zeros((nf,nd))
    P=np.zeros((nf,nd))
    M=np.zeros((nf,nd))
    #A=np.zeros((nf,nd))
    #V=np.zeros((nf,nd))
    X=qq.real*qq.real + qq.imag*qq.imag
    for ii,j1 in enumerate(ni):
        j2=min(nq,j1+ns)
        T[ii] = np.mean(tq[j1:j2])
        U=X[:,j1:j2]
        Q[:,ii] = np.mean(U,axis=1)                         # mean power
        P[:,ii] = np.max(U,axis=1)                          # max power
        M[:,ii] = np.median(U,axis=1)                       # median power
        #A[:,ii] = np.mean(np.abs(U[:,1:]-U[:,:-1]),axis=1)  # ACI mean power variation
        #V[:,ii] = np.std(U,axis=1)                          # STD power
    return {"T":T, "F":F, "Q":Q, "P":P, "M":M}#, "A":A, "V":V }

def detection(res2):
    S=res2['P']/res2['M']   # peak/median
    return {"S":S}

def doProcessing(filepath,params):
    fs, data = load_wav(filepath)

    # preprocess
    if int(params['diff'])>0:
        xx=0*data
        xx[1:]=data[1:]-data[:-1]
    else:
        xx=data-data.mean()

    # spectrogram
    nfft = int(params["nfft"])
    nw   = int(params["rwin"]*nfft)       # window length
    ns   = int((1-params["over"])*nw)     # step size
    fq,tq,qq=spectrogram(xx,fs,nfft=nfft,nw=nw,ns=ns)

    # accumulate 
    na   = params["twin"]
    fmin = params['fmin']
    fmax = params['fmax']
    res2= analysis(fq,tq,qq,na,fmin,fmax)

    # detection
    res3= detection(res2)

    return {"filepath": filepath,
            "fs": fs,
            "freqs": fq,
            "times":tq,
            **res2,
            **res3
            }

class paramClass(tk.Toplevel):
    def __init__(self, master, txtvar,titles, groups, **kwargs):
        super().__init__(master, **kwargs)

        cols=np.array([np.double(txtvar[1][ii].get()).astype(int) for ii in range(len(txtvar[0]))])
        #
        frame1=tk.Frame(self,border=1,borderwidth=1,relief="solid",padx=5,pady=5)
        frame1.pack()
        #
        for kk in range(max(groups)[0]+1):
            framex=tk.Frame(frame1,bd = 1,relief='solid',padx=5,pady=5)
            framex.grid(row=0,column=kk,sticky='N')
            #
            for jj in range(max(cols)+1):
                frame2=tk.LabelFrame(framex,text=titles[jj],padx=5,pady=5)
                colx=np.where((cols==jj )& (groups[jj][0]==kk))[0]
                #
                for ii,col in enumerate(colx):
                    itype=np.double(txtvar[3][col].get()).astype(int)
                    if itype==0:
                        w=10
                    else:
                        w=itype
                    lbl1 = tk.Label(frame2, text=txtvar[0][col])
                    ent1 = tk.Entry(frame2, textvariable=txtvar[2][col],width=w) 
                    lbl2 = tk.Label(frame2, text=txtvar[4][col])
                    #
                    lbl1.grid(row=ii,column=0,padx=10,pady=5,sticky="E")
                    ent1.grid(row=ii,column=1,padx=10,pady=5,sticky="W")
                    lbl2.grid(row=ii,column=2,padx=10,pady=5,sticky="W")
                column,row=groups[jj]
                frame2.grid(row=row,column=column,sticky='N')

        self.wait_window()
        return

#=================================================================
class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.wm_title("microPAM Viewer (WMXZ)")
        self.geometry("1400x800+10+10")
        #
        # Bind the window close (X) button
        self.protocol("WM_DELETE_WINDOW", self.on_close)
        #
        self.file_list   = []   # list of file paths
        self.results     = {}   # path → result dict
        self.file_status = {}   # path → status string

        self._running = -1
        #
        # default parameters with gui info
        self.param_titles=['General','Time window', 'Spectral Window', 'PSD', 'Detector']
        self.param_groups=[[0,0],     [1,0],        [1,1],             [1,2],  [2,0]]
        self.param={
                    'ich':   [0, 0,    2,'channel #'],
                    'diff':  [0, 0,    2,'diff filter'],
                    'iplt':  [0, 0,    2,'do plotting'],
                    'mdyn':  [0, 60,   4,'max dynamic range (dB)'],
                    'nfft':  [1, 2048, 0,'FFT size [pts]'],
                    'rwin':  [1, 0.5,  0,'Time window [rel nfft]'],
                    'over':  [1, 0.5,  0,'Overlap [rel rwin]'],
                    'fmin':  [2,    0, 0,'Freq. min [Hz]'],
                    'fmax':  [2, 48000,0,'Freq. max [Hz]'],
                    'twin':  [3, 1,    0,'analysis window (sec)'],
                    'perc':  [4, 95,   0,'signal percentile'],
                    'thres': [4, 2,    0,'threshold'],
                    'gap':   [4, 0.05, 0,'min det. gap']}
        #
        # define graphics to used in _show_results
        self.plots={'PSD': ("Q",np.mean,[]), 
                    "Peak":("P",np.max, []),
                    "SNR": ("S",np.max, [])}

        self._build_ui()

    def on_close(self):
        """Handle window close event."""
        print("Closing application...")
        #self.destroy()  # Properly destroy the Tkinter window (needed?)
        sys.exit(0)  # Ensure Python process exits

    def _build_ui(self):
        # ── Menu ──
        menu = tk.Menu()
        self.config(menu=menu)
        self._build_menu(menu)

        # ── Top toolbar ──
        toolbar = tk.Frame(self, pady=7, padx=10,bd = 1,relief='solid')
        toolbar.pack(fill="x", side="top")
        self._build_toolbar(toolbar)

        # ── main area left: file list ── right: plotting area -──
        body = tk.Frame(self,bd = 0,relief='solid')
        body.pack(fill="both", expand=True)

        ww=350
        left = tk.Frame(body, width=ww, padx=8, pady=8,bd = 1,relief='solid')
        left.pack(fill="y", side="left")
        left.pack_propagate(False)

        # Summary
        sum = tk.LabelFrame(left, width=ww, height=100, padx=1, pady=1,bd = 1,relief='solid',text='Summary')
        sum.pack(fill="y", side="top")
        self._build_summary(sum)

        # File list
        flist = tk.LabelFrame(left, width=ww, bd = 1,relief='solid',text='File list')
        flist.pack(fill="y", expand=True, side='bottom')
        self._build_file_list(flist)

        # plot area
        right = tk.Frame(body,bd = 0,relief='solid', padx=8)
        right.pack(fill="both", expand=True, side="left")
        self._build_plots(right)

    #----------------------------------------------------------------------------------
    def _build_menu(self,menu):
        fileMenu = tk.Menu(menu,tearoff=0)
        menu.add_cascade(label="Parameters", menu=fileMenu)
        fileMenu.add_command(label="Edit",command=self._edit_parameters)
        fileMenu.add_command(label="Load",command=self._load_parameters)
        fileMenu.add_command(label="Save",command=self._save_parameters)

        resultMenu = tk.Menu(menu,tearoff=0)
        menu.add_cascade(label="Results", menu=resultMenu)
        resultMenu.add_command(label="Load",command=self._load_results)
        resultMenu.add_command(label="Save",command=self._save_results)

    def _edit_parameters(self):
        #encode for passing to paramClass
        param_keys=list(self.param.keys())
        txtvar = [param_keys,                                                   # 0
                  [tk.StringVar(value=self.param[x][0]) for x in param_keys],   # 1
                  [tk.StringVar(value=self.param[x][1]) for x in param_keys],   # 2
                  [tk.StringVar(value=self.param[x][2]) for x in param_keys],   # 3
                  [                   self.param[x][3]  for x in param_keys]]   # 4    
        #get input
        paramClass(self, txtvar,self.param_titles,self.param_groups)
        #decode parameters
        for ii,key in enumerate(param_keys):
            self.param[key][1]=np.double(txtvar[2][ii].get())
        return

    def _save_parameters(self):
        np.save('Parameters.npy', self.param)
        return

    def _load_parameters(self):
        self.param={np.load('Parameters.npy',allow_pickle='TRUE').item()}
        return

    def _get_params(self):
        def _get(key):
            return {key: float(self.param[key][1])}
        #
        return {**_get('diff'),
                **_get('nfft'),
                **_get('rwin'),
                **_get('over'),
                **_get('twin'),
                **_get('fmin'),
                **_get('fmax'),
                **_get('perc'),
                **_get('thres'),
                **_get('gap')
                }

    def _load_results(self):
        self.results = np.load('Results.npy',allow_pickle='TRUE').item()
        self._add_paths(self.results,'done')
        self.after(0, lambda: self._file_batch_done())

    def _save_results(self):
        np.save('Results.npy',self.results)

    #------------------------------------------------------------------------
    def _build_toolbar(self,parent):
        def btn(text, cmd, bold=False):
            f = ("Helvetica", 10, "bold") if bold else ("Helvetica", 10)
            b = tk.Button(parent, text=text, command=cmd,
                          font=f,  padx=11, pady=5,
                          cursor="hand2")
            b.pack(side="left", padx=3)
            return b

        btn("📁  Add Folder", self._add_folder)

        self.btn_run = btn("▶  Start", self._start_batch, bold=True)

    #
    def _add_folder(self):
        folder = filedialog.askdirectory()
        if not folder:
            return
        paths = []
        for root, _, files in os.walk(folder):
            for f in sorted(files):
                if f.lower().endswith(".wav") or f.lower().endswith(".bin"):
                    paths.append(os.path.join(root, f))
        self._add_paths(paths)

    def _add_paths(self, paths, status='pending'):
        added = 0
        for p in paths:
            if p not in self.file_list:
                self.file_list.append(p)
                self.file_status[p] = status
                self.tree.insert("", "end", iid=p,
                                 values=(os.path.basename(p),status, "—"))
                added += 1
        self._update_summary()
        #self.status_var.set(f"Aggiunti {added} file. Totale: {len(self.file_list)}")

        self._running=0
        return
    #
    def _start_batch(self):
        if self._running==0:
            self.btn_run.config(text="⏹  Stop",fg='Red')
            self._running=1
            threading.Thread(target=self._batch_thread, daemon=True).start()
        else:
            self.btn_run.config(text="▶  Start",fg='Green')
            self._running=0
        return

    def _batch_thread(self):
        #
        for path in self.file_list:
            if self.file_status[path] == 'done':
                continue
            if self._running==0:
                break
            ret = self._process_file(path)
        self.after(0, lambda: self._file_batch_done())

    #-------------------------------------------------------------------------------
    def _build_summary(self,parent):
        self.lbl_summary = {}
        for key, lbl in [("total",     "Files in list"),
                         ("done",      "Files completed"),
                         ("tot_events","Tot. events")]:
            f = tk.Frame(parent)
            f.pack(fill="x", pady=1)
            #
            tk.Label(f, text=lbl,
                     font=("Helvetica", 8), width=15, anchor="e").pack(side="left")
            #
            v = tk.Label(f, text="—",
                         font=("Helvetica", 8, "bold"), anchor="e")
            v.pack(side="right")
            self.lbl_summary[key] = v
        return

    def _update_summary(self):
        total = len(self.file_list)
        done=0
        done  = sum(1 for s in self.file_status.values() if s == 'done')
        tot_ev=0
        #tot_ev = sum(r["n_events"] for r in self.results.values())
        #
        self.lbl_summary["total"].config(text=str(total))
        self.lbl_summary["done"].config(text=str(done))
        self.lbl_summary["tot_events"].config(text=str(tot_ev))

    #-----------------------------------------------------------------------------
    def _build_file_list(self,parent):
        # Treeview
        list_dict={   "name": ["File",  'e',    175],
                    "status": ["Status",'center',70],
                    "events": ['Events','center',40]}

        cols= list_dict.keys()
        self.tree = ttk.Treeview(parent, columns=list(cols), show="headings",
                                  selectmode="extended")
        for key in cols:
            self.tree.heading(key, text=list_dict[key][0])            
            self.tree.column(key,  width=list_dict[key][2], anchor=list_dict[key][1])

        style = ttk.Style()
        style.theme_use("default")
        style.configure("Treeview",rowheight=22, font=("Helvetica", 8))
        style.configure("Treeview.Heading",
                         font=("Helvetica", 8, "bold"), relief="flat")
        style.map("Treeview", background=[("selected", 'lightgreen')],
                              foreground=[("selected", "black")])

        sb = ttk.Scrollbar(parent, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")

        self.tree.bind("<<TreeviewSelect>>", self._on_tree_select)

    def _on_tree_select(self, _event=None):
        sel = self.tree.selection()
        if not sel:
            return
        path = sel[0]

        if path not in self.results:
            ret=self._process_file(path)

        if self.param['iplt'][1]==0: # in case it is not plotted via _file_done()
            self._show_result(path)

        self.tree.selection_remove(sel)
        return

    #-------------------------------------------------------------------------------
    def _process_file(self,path):
        self._set_file_status(path, 'running')
        self.canvas.flush_events()
        #
        try:
            params= self._get_params()
        except ValueError as e:
            self.after(0, lambda: messagebox.showerror("Parameters not valid", str(e)))
            self._running = 0
            return 0
        #
        try:
            result = doProcessing(path, params)
            self.results[path] = result
            self.after(0, lambda p=path, r=result: self._file_done(p, r))
        except Exception as e:
            err = str(e)
            self.after(0, lambda p=path, e=err: self._file_error(p, e))
            return 0
        return 1

    def _set_file_status(self, path, status):
        self.file_status[path] = status
        vals = self.tree.item(path, "values")
        self.tree.item(path, values=(vals[0], status, vals[2]))

    def _file_done(self, path, result):
        self.file_status[path] = 'done'
        self.tree.item(path, 
                       values=(os.path.basename(path), 'done', '0'))#str(result["n_events"])))
        #
        self._update_summary()
        #
        if self.param['iplt'][1]==1 :
            self._show_result(path)

    def _file_error(self, path, err):
        self.file_status[path] = 'error'
        #print(err)
        self.tree.item(path, values=( os.path.basename(path), 'error', "—"))

    def _file_batch_done(self):
        self._show_result(path=None)

    #-------------------------------------------------------------------------------
    def _build_plots(self,parent):
        self.fig, self.axs = plt.subplots(3, 1, figsize=(10, 6.5),
                                        sharex=True,sharey=True,
                                        layout='constrained')#, tight_layout=True)

        # containing the Matplotlib figure
        self.canvas = FigureCanvasTkAgg(self.fig)

        self.canvas = FigureCanvasTkAgg(self.fig, master=parent)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)
        nav = NavigationToolbar2Tk(self.canvas, parent)
        nav.config(bg='white')
        nav.update()
        return

    def _show_result(self,path=None):
        #
        mdyn=self.param['mdyn'][1]
        O=self.plots
        o_keys=O.keys()

        #
        def _pretty_xtick(tvec):
            dt=tvec[-1]-tvec[0]
            if dt>5*3600*24:
                tvec /=(3600*24)
                tdim='(day)'
            elif dt>5*3600:
                tvec /= 3600
                tdim='(hour)'
            elif dt>5*60:
                tvec /= 60
                tdim='(min)'
            else:
                tdim='(sec)'
            return tvec,tdim

        def _plot_heatmap(axs,ii, M,t_ext,f_ext,title):
            ax=axs[ii]

            axm=ax.images
            if len(axm)>0:
                axm[-1].colorbar.remove()
            ax.cla()
            t_ext,tdim =_pretty_xtick(np.array(t_ext))
            maxM=np.percentile(M,99)#.max(M)
            minM=np.max([maxM-mdyn, np.min(M)])
            clim=[minM,maxM]
            im    = ax.imshow(M, aspect='auto', origin='lower',
                            extent=[*t_ext, *f_ext],
                            cmap='jet',clim=clim)
            ax.set_title(title, fontsize=12, pad=4)
            if ax==axs[-1]:
                ax.set_xlabel("Time"+tdim, fontsize=12)
            ax.set_ylabel("Freq (kHz)", fontsize=12)
            ax.tick_params(labelsize=12)
            ax.grid(True, alpha=0.3)

            self._colorbar = self.fig.colorbar(im, ax=ax, fraction=0.015, pad=0.01)
            self._colorbar.ax.tick_params(labelsize=12)

        # processimg results
        if path==None:
            # accumulate and show global results 
            t_ext=None
            f_ext=None
            X=[[] for _ in range(len(o_keys))]
            for key in self.results:
                r=self.results[key]
                if t_ext==None:
                    t_ext = [r["T"][0], r["T"][-1]]
                else:
                    t_ext[1] += r["T"][-1]
                if f_ext==None:
                    f_ext = [r["F"][0] / 1000, r["F"][-1] / 1000]
                #
                for ii,key in enumerate(o_keys):
                    f=O[key][1]
                    x=r[O[key][0]]
                    # X[ii].append(f(x,axis=1))
                    for jj in range(6):
                        j1=jj*10
                        j2=j1+10
                        y=f(x[:,j1:j2],axis=1)
                        X[ii].append(y)

            # replace data in dictionary
            for ii,key in enumerate(o_keys):
                O[key]= (*O[key][:-1],X[ii])
            #
            for ii,key in enumerate(o_keys):
                _plot_heatmap(self.axs,ii,dB(np.array(O[key][-1])).T,t_ext,f_ext, key)
        else:
            # plot individual result
            fname = os.path.basename(path)
            #print(fname)

            r = self.results[path]

            t_ext = [r["T"][0], r["T"][-1]]
            f_ext = [r["F"][0] / 1000, r["F"][-1] / 1000]

            for ii,key in enumerate(o_keys):
                _plot_heatmap(self.axs,ii,dB(r[O[key][0]]),t_ext,f_ext, f"{key} - {fname}")

            #for ev in r["events"]:
            #    self.axs[0].axvline(ev['time'], color='red', alpha=0.65, lw=0.8)
            #
            # ── Impulsivity Index ─────────────────────────────────────────────────
            #ax = self.axs[1]
            #ax.cla()
            #_plot_heatmap(ax,r["P"].T,t_ext,f_ext, f"Peak — {fname}")
            #ax.plot(r["times"], r["ii"], lw=0.8, alpha=0.9, label="II(t)")
            #ax.axhline(r["threshold"], color='gray', lw=1.2, ls='--',
            #        label=f"Threshold ({r['threshold']:.3f})")
            #ev_t = [e['time'] for e in r["events"]]
            #ev_v = [e['ii_value'] for e in r["events"]]
            #if ev_t:
            #    ax.scatter(ev_t, ev_v, color='red', s=18, zorder=5,
            #            label=f"{r['n_events']} events")
            #ax.set_title("Impulsivity Index II(t)", fontsize=9, pad=4)
            #ax.set_xlabel("Time (s)", fontsize=8)
            #ax.set_ylabel("II (dB)", fontsize=8)
            #ax.tick_params(labelsize=7)
            #ax.legend(fontsize=7, loc='lower right')
            #ax.grid(True, alpha=0.3)
            #ax = self.axs[2]
            #ax.cla()
            #_plot_heatmap(ax,r["A"].T,t_ext,f_ext, f"ACI — {fname}")

        #
        self.canvas.draw()
        return
#=============================================================================
def main():
    app=App()
    app.mainloop()
#
if __name__ == '__main__':
    main()
