"""
micropam_browser.py — microPAM_V4 file browser
================================================
Lists microPAM data files (.wav / .bin / .spc / .int / .vae) in a chosen
directory.  Right-click on any file to open a popup with the parsed header.

Usage
-----
    python Python/micropam_browser.py [start_directory]
"""

import sys
import os
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from datetime import datetime

import numpy as np

# micropam_reader lives in the same directory; add it to path.
sys.path.insert(0, str(Path(__file__).parent))
from micropam_reader import MicroPAMFile, _parse_wav_header, HEADER_BYTES

# ── constants ──────────────────────────────────────────────────────────────────
EXTENSIONS   = {".wav", ".bin", ".spc", ".int", ".vae"}
MODE_LABELS  = {0: "WAV (raw PCM)", 1: "BIN (compressed)",
                2: "SPC (spectrum)", 3: "INT (intensity)", 4: "VAE (classifier)"}

COL_NAME  = "File"
COL_SIZE  = "Size"
COL_DATE  = "Modified"
COL_MODE  = "Mode"
COL_FSAMP = "fsamp"
COL_NCH   = "nch"
COL_DUR   = "Dur / Size"

COLUMNS = (COL_NAME, COL_SIZE, COL_DATE, COL_MODE, COL_FSAMP, COL_NCH, COL_DUR)


# ── helpers ────────────────────────────────────────────────────────────────────

def _fmt_size(nbytes: int) -> str:
    if nbytes < 1024:
        return f"{nbytes} B"
    elif nbytes < 1024**2:
        return f"{nbytes/1024:.1f} KB"
    else:
        return f"{nbytes/1024**2:.1f} MB"


def _fmt_duration(info: dict, path: Path) -> str:
    """Duration (mode 0, exact from file size) or file size for other modes."""
    fs  = info.get("sample_rate", 0)
    nch = info.get("n_channels",  0)
    pm  = info.get("proc_mode",   -1)
    nbytes = path.stat().st_size - HEADER_BYTES
    if pm == 0 and fs > 0 and nch > 0:
        secs = nbytes / (nch * 4 * fs)
        if secs > 0:
            m, s = divmod(int(secs), 60)
            h, m = divmod(m, 60)
            return f"{h:02d}:{m:02d}:{s:02d}" if h else f"{m:02d}:{s:02d}"
    return _fmt_size(nbytes)


def _safe_parse(path: Path) -> dict | None:
    try:
        raw = path.read_bytes()[:HEADER_BYTES]
        if len(raw) < HEADER_BYTES:
            return None
        return _parse_wav_header(raw)
    except Exception:
        return None


# ── header popup window ────────────────────────────────────────────────────────

class HeaderPopup(tk.Toplevel):
    """Popup showing parsed header fields for one file."""

    def __init__(self, parent, path: Path, info: dict):
        super().__init__(parent)
        self.title(f"Header — {path.name}")
        self.resizable(True, True)
        self.minsize(480, 380)

        # ── close on Escape ──────────────────────────────────────────────────
        self.bind("<Escape>", lambda _: self.destroy())

        # ── scrollable text area ─────────────────────────────────────────────
        frame = ttk.Frame(self, padding=8)
        frame.pack(fill=tk.BOTH, expand=True)

        txt = tk.Text(frame, wrap=tk.NONE, font=("Courier New", 10),
                      relief=tk.FLAT, bg="#ffffff", fg="#1e1e1e",
                      insertbackground="black", selectbackground="#cce5ff")
        vsb = ttk.Scrollbar(frame, orient=tk.VERTICAL,   command=txt.yview)
        hsb = ttk.Scrollbar(frame, orient=tk.HORIZONTAL, command=txt.xview)
        txt.configure(yscrollcommand=vsb.set, xscrollcommand=hsb.set)

        hsb.pack(side=tk.BOTTOM, fill=tk.X)
        vsb.pack(side=tk.RIGHT,  fill=tk.Y)
        txt.pack(side=tk.LEFT,   fill=tk.BOTH, expand=True)

        # colour tags
        txt.tag_configure("key",   foreground="#0050a0")
        txt.tag_configure("val",   foreground="#a05000")
        txt.tag_configure("head",  foreground="#006000", font=("Courier New", 10, "bold"))
        txt.tag_configure("sep",   foreground="#999999")

        def add(key, val="", key_tag="key", val_tag="val"):
            txt.insert(tk.END, f"  {key:<26}", key_tag)
            txt.insert(tk.END, f"{val}\n", val_tag)

        def sep(title=""):
            line = f"── {title} " + "─" * max(0, 50 - len(title)) if title else "─" * 54
            txt.insert(tk.END, f"{line}\n", "sep")

        stat = path.stat()

        sep("File")
        add("Path",        str(path))
        add("Size",        _fmt_size(stat.st_size))
        add("Modified",    datetime.fromtimestamp(stat.st_mtime).strftime("%Y-%m-%d %H:%M:%S"))

        sep("WAV format")
        add("Channels",    str(info.get("n_channels", "?")))
        add("Sample rate", f"{info.get('sample_rate', '?')} Hz")
        add("Bit depth",   f"{info.get('bits_per_sample', '?')} bit")

        sep("Acquisition (IKEY)")
        pm = info.get("proc_mode", -1)
        add("PROC_MODE",   f"{pm}  —  {MODE_LABELS.get(pm, 'unknown')}")
        add("UID",         info.get("uid",       ""))
        add("Version",     info.get("version",   ""))
        add("fsamp_khz",   str(info.get("fsamp_khz", "?")))
        add("SHIFT",       str(info.get("shift",     "?")))
        add("NBUF_PROC",   str(info.get("nbuf_proc", "?")))
        add("NAVG",        str(info.get("navg",      "?")))
        add("Again (dB)",  str(info.get("again",     "?")))
        add("Vsens",       str(info.get("vsens",     "?")))

        t_acq = info.get("t_acq", 0)
        t_on  = info.get("t_on",  0)
        t_rep = info.get("t_rep", 0)
        add("t_acq (s)",   str(t_acq))
        add("t_on  (s)",   str(t_on))
        add("t_rep (s)",   str(t_rep))

        h_rec = info.get("h_rec", [])
        if any(h_rec):
            add("h_rec",   str(h_rec))

        sep("Timestamp (filename)")
        # try to parse datetime from filename  e.g. 20260628_133600
        stem = path.stem
        parts = stem.split("_")
        ts = ""
        for i, p in enumerate(parts):
            if len(p) == 8 and p.isdigit() and i + 1 < len(parts):
                nxt = parts[i + 1]
                if len(nxt) == 6 and nxt.isdigit():
                    ts = f"{p[:4]}-{p[4:6]}-{p[6:]} {nxt[:2]}:{nxt[2:4]}:{nxt[4:]}"
                    break
        add("Recording start", ts or "(not in filename)")

        sep("INFO chunks")
        for tag, val in info.get("chunks", {}).items():
            add(tag, val[:80])

        sep()
        add("Duration (est.)", _fmt_duration(info, path))

        txt.configure(state=tk.DISABLED)

        # close button
        ttk.Button(self, text="Close", command=self.destroy).pack(pady=(0, 8))

        # centre relative to parent
        self.update_idletasks()
        px = parent.winfo_rootx() + (parent.winfo_width()  - self.winfo_width())  // 2
        py = parent.winfo_rooty() + (parent.winfo_height() - self.winfo_height()) // 2
        self.geometry(f"+{max(0,px)}+{max(0,py)}")
        self.focus_set()


# ── data viewer window ────────────────────────────────────────────────────────

class DataViewer(tk.Toplevel):
    """Modal-less window that plots data from one microPAM file using matplotlib."""

    _CHAN_COLORS = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728",
                    "#9467bd", "#8c564b", "#e377c2", "#7f7f7f"]

    def __init__(self, parent, path: Path, info: dict):
        super().__init__(parent)
        self.title(f"Data — {path.name}")
        self.geometry("1080x680")
        self.minsize(640, 400)
        self.bind("<Escape>", lambda _: self.destroy())

        self._path = path
        self._info = info
        self._pm   = info.get("proc_mode", -1)

        try:
            from matplotlib.backends.backend_tkagg import (
                FigureCanvasTkAgg, NavigationToolbar2Tk)
            import matplotlib.figure as mplf
            self._Figure              = mplf.Figure
            self._FigureCanvasTkAgg   = FigureCanvasTkAgg
            self._NavigationToolbar2Tk = NavigationToolbar2Tk
        except ImportError:
            tk.Label(self, text="matplotlib is not installed.\n"
                     "Run: pip install matplotlib", font=("Courier New", 11),
                     fg="#cc0000").pack(expand=True)
            ttk.Button(self, text="Close", command=self.destroy).pack(pady=6)
            return

        self._fig = self._Figure(figsize=(13, 7), dpi=96, facecolor="#f8f8f8",
                                 layout="constrained")

        frame = ttk.Frame(self)
        frame.pack(fill=tk.BOTH, expand=True)

        self._canvas = self._FigureCanvasTkAgg(self._fig, master=frame)
        toolbar = self._NavigationToolbar2Tk(self._canvas, frame)
        toolbar.update()
        self._canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        ttk.Button(self, text="Close", command=self.destroy).pack(pady=(0, 6))

        self._load_and_plot()

    # ── data loading ──────────────────────────────────────────────────────────

    def _load_and_plot(self):
        self.configure(cursor="watch")
        self.update()
        try:
            result = MicroPAMFile(self._path).read_all()
            pm = self._pm
            if pm in (0, 1):
                self._plot_mode01(result)
            elif pm == 2:
                self._plot_mode2(result)
            elif pm == 3:
                self._plot_mode3(result)
            elif pm == 4:
                self._plot_mode4(result)
            else:
                self._plot_unsupported(pm)
        except Exception as exc:
            self._plot_error(str(exc))
        finally:
            self.configure(cursor="")
            self._canvas.draw()

    # ── layout: modes 0 and 1 ────────────────────────────────────────────────

    def _plot_mode01(self, result):
        data = result["data"]                  # (nframes, nch) int32
        fs   = self._info["sample_rate"]
        nch  = data.shape[1]
        t    = np.arange(data.shape[0]) / fs

        from matplotlib.gridspec import GridSpec
        # 2 columns: col 0 = data (all axes), col 1 = colorbar for spectrogram only.
        # Time series axes occupy col 0 only; constrained_layout sizes col 0 uniformly
        # so spectrogram and time series have identical x-axis widths.
        n_rows = nch + 1
        gs = GridSpec(n_rows, 2, figure=self._fig,
                      width_ratios=[30, 1], height_ratios=[1] * nch + [1])

        # ── time series (col 0, rows 0..nch-1) ───────────────────────────────
        ax0 = None
        for ch in range(nch):
            ax = self._fig.add_subplot(gs[ch, 0], sharex=ax0)
            if ax0 is None:
                ax0 = ax
            ax.plot(t, data[:, ch], lw=0.4,
                    color=self._CHAN_COLORS[ch % len(self._CHAN_COLORS)])
            ax.set_ylabel(f"Ch{ch}", fontsize=7, rotation=0, labelpad=22)
            ax.tick_params(labelsize=6)
            ax.margins(x=0)
            if ch == 0:
                ax.set_title("Time series", fontsize=8)
            if ch < nch - 1:
                ax.set_xticklabels([])
            else:
                ax.set_xlabel("Time (s)", fontsize=7)

        # ── spectrogram (col 0, last row) + colorbar (col 1, last row) ───────
        ax_spc = self._fig.add_subplot(gs[nch, 0], sharex=ax0)
        cax    = self._fig.add_subplot(gs[nch, 1])
        im = self._draw_spectrogram(ax_spc, data[:, 0].astype(np.float64),
                                    fs, title="Spectrogram  ch0")
        if im is not None:
            self._fig.colorbar(im, cax=cax)

    # ── layout: mode 2 ───────────────────────────────────────────────────────

    def _plot_mode2(self, result):
        data  = result["data"]           # (N, nch)  int32 spectrum magnitudes
        nch   = data.shape[1]
        # NBUF_PROC (=NDATA=NBUF_I2S) is stored channel-interleaved: nch channels
        # × nsamp frequency bins.  After decompression each record has shape
        # (nsamp, nch) where nsamp = nbuf_proc // nch = NSAMP = NFFT/2.
        nbuf_proc = self._info.get("nbuf_proc", 2048) or 2048
        nsamp     = nbuf_proc // nch   # number of frequency bins per frame
        fs        = self._info["sample_rate"]

        n_full = (data.shape[0] // nsamp) * nsamp
        if n_full < nsamp:
            self._plot_error("File too short to display a spectrogram (mode 2).")
            return

        # reshape to (n_time, n_freq, nch)
        arr = data[:n_full].reshape(-1, nsamp, nch).astype(np.float64)

        # frequency axis: nsamp bins from 0 .. fs/2
        freqs = np.linspace(0, fs / 2, nsamp)

        from matplotlib.gridspec import GridSpec
        gs = GridSpec(nch, 1, figure=self._fig)

        ax0 = None
        for ch in range(nch):
            ax = self._fig.add_subplot(gs[ch], sharex=ax0, sharey=ax0)
            if ax0 is None:
                ax0 = ax
            spec = arr[:, :, ch].T                # (n_freq, n_time)
            spec_db = 20 * np.log10(np.abs(spec) + 1.0)
            vmax = spec_db.max()
            im = ax.imshow(spec_db, aspect="auto", origin="lower", cmap="inferno",
                           vmin=vmax - 60, vmax=vmax,
                           extent=[0, spec_db.shape[1], freqs[0], freqs[-1]])
            self._fig.colorbar(im, ax=ax, fraction=0.03, pad=0.01)
            ax.set_title(f"Ch{ch}", fontsize=8)
            ax.set_ylabel("Freq (Hz)", fontsize=7)
            ax.tick_params(labelsize=6)
            if ch < nch - 1:
                ax.set_xticklabels([])
            else:
                ax.set_xlabel("Frame", fontsize=7)

    # ── spectrogram helper ────────────────────────────────────────────────────

    def _draw_spectrogram(self, ax, sig, fs, *, title="", nperseg=512, noverlap=256):
        step   = nperseg - noverlap
        nsteps = max(1, (len(sig) - nperseg) // step)
        if nsteps < 2:
            ax.set_title(title + " (insufficient data)", fontsize=8)
            return None
        window = np.hanning(nperseg)
        frames = np.stack(
            [sig[i * step: i * step + nperseg] * window for i in range(nsteps)]
        )                                                    # (nsteps, nperseg)
        spec    = np.abs(np.fft.rfft(frames, axis=1))       # (nsteps, nperseg//2+1)
        spec_db = 20 * np.log10(spec + 1e-6)

        freqs = np.fft.rfftfreq(nperseg, 1 / fs)
        times = np.arange(nsteps) * step / fs

        vmax = spec_db.max()
        im = ax.imshow(spec_db.T, aspect="auto", origin="lower", cmap="inferno",
                       vmin=vmax - 60, vmax=vmax,
                       extent=[times[0], times[-1], freqs[0], freqs[-1]])
        ax.set_ylabel("Freq (Hz)", fontsize=7)
        ax.set_xlabel("Time (s)", fontsize=7)
        ax.set_title(title, fontsize=8)
        ax.tick_params(labelsize=6)
        return im

    # ── layout: mode 3 ───────────────────────────────────────────────────────

    def _plot_mode3(self, result):
        data = result["data"]          # (N, 3)  int32  [Ix, Iy, Iz] per freq bin
        nch  = data.shape[1]           # always 3 for mode 3
        nbuf_proc = self._info.get("nbuf_proc", 0) or (3 * 512)
        nsamp     = nbuf_proc // nch   # frequency bins per frame (= NSAMP)
        fs        = self._info["sample_rate"]

        n_full = (data.shape[0] // nsamp) * nsamp
        if n_full < nsamp:
            self._plot_error("File too short to display intensity (mode 3).")
            return

        # (n_time, nsamp, 3)
        arr = data[:n_full].reshape(-1, nsamp, nch).astype(np.float64)
        Ix  = arr[:, :, 0]
        Iy  = arr[:, :, 1]
        Iz  = arr[:, :, 2]
        mag = np.sqrt(Ix**2 + Iy**2 + Iz**2)   # (n_time, nsamp)

        freqs  = np.linspace(0, fs / 2, nsamp)
        extent = [0, arr.shape[0], freqs[0], freqs[-1]]

        # log-scale helpers — avoid the dynamic-range collapse from sparse data
        def _log_mag(a):
            return np.log10(a + 1.0)                          # 0 → 0, monotone

        def _symlog(a):
            return np.sign(a) * np.log10(np.abs(a) + 1.0)    # symmetric around 0

        mag_l  = _log_mag(mag)
        Ix_l   = _symlog(Ix)
        Iy_l   = _symlog(Iy)
        Iz_l   = _symlog(Iz)

        from matplotlib.gridspec import GridSpec
        gs  = GridSpec(4, 1, figure=self._fig, hspace=0.35)
        ax0 = None

        def _add_panel(data2d, title, cmap, symmetric, ax_ref, idx):
            ax = self._fig.add_subplot(gs[idx], sharex=ax_ref, sharey=ax_ref)
            vmax = float(np.abs(data2d).max()) or 1.0
            vmin = -vmax if symmetric else 0.0
            im = ax.imshow(data2d.T, aspect="auto", origin="lower", cmap=cmap,
                           vmin=vmin, vmax=vmax, extent=extent)
            self._fig.colorbar(im, ax=ax, fraction=0.03, pad=0.01)
            ax.set_title(title, fontsize=8)
            ax.set_ylabel("Freq (Hz)", fontsize=7)
            ax.tick_params(labelsize=6)
            return ax

        ax0      = _add_panel(mag_l, "|I|  log10(mag+1)",    "inferno", False, None, 0)
        _add_panel(Ix_l,  "Ix  sign*log10(|Ix|+1)",  "RdBu_r", True,  ax0,  1)
        _add_panel(Iy_l,  "Iy  sign*log10(|Iy|+1)",  "RdBu_r", True,  ax0,  2)
        ax_last = _add_panel(Iz_l, "Iz  sign*log10(|Iz|+1)", "RdBu_r", True,  ax0,  3)
        ax_last.set_xlabel("Frame", fontsize=7)

    # ── layout: mode 4 ───────────────────────────────────────────────────────

    def _plot_mode4(self, result):
        millis    = np.asarray(result["millis"],           dtype=np.float64)
        det_exc   = np.asarray(result["detection_excess"], dtype=np.float64)
        vae_det   = np.asarray(result["vae_detect"],       dtype=np.float64)  # (N, 4)
        sig_flag  = np.asarray(result["signal_flag"],      dtype=np.int8)

        if millis.size == 0:
            self._plot_error("No records decoded from mode 4 file.")
            return

        t = (millis - millis[0]) / 1000.0      # relative seconds

        from matplotlib.gridspec import GridSpec
        n_rows = 5
        gs  = GridSpec(n_rows, 1, figure=self._fig)
        ax0 = None

        def _ts_ax(idx, label, color):
            ax = self._fig.add_subplot(gs[idx], sharex=ax0)
            ax.set_ylabel(label, fontsize=7, rotation=0, labelpad=46)
            ax.tick_params(labelsize=6)
            ax.margins(x=0)
            if idx < n_rows - 1:
                ax.set_xticklabels([])
            else:
                ax.set_xlabel("Time (s)", fontsize=7)
            return ax

        DETECT_THR = 3.0
        Dsnr = det_exc + DETECT_THR

        # ── ax 0: detection excess ────────────────────────────────────────────
        ax = _ts_ax(0, "Det excess", "#333333")
        ax0 = ax
        ax.plot(t, det_exc, lw=0.6, color="#1f77b4")
        ax.fill_between(t, det_exc, 0,
                        where=(det_exc > 0), alpha=0.25, color="#1f77b4")
        ax.axhline(0, color="#cc0000", lw=0.7, ls="--")
        ax.set_title("Detection excess  (Dsnr - DETECT_THR)", fontsize=8)

        # ── noise-frame threshold: mean + 3*sigma per VAE ────────────────────
        noise = sig_flag == 0
        thr = np.zeros(4)
        for v in range(4):
            s = vae_det[noise, v] if noise.any() else vae_det[:, v]
            thr[v] = s.mean() + 3.0 * s.std() if len(s) > 1 else vae_det[:, v].max()
        y_top = float(vae_det.max()) * 1.1   # shared y-ceiling for all VAE axes

        # ── ax 1-4: per-VAE anomaly scores ───────────────────────────────────
        vae_colors = ["#e377c2", "#ff7f0e", "#2ca02c", "#d62728"]
        ax_vae0 = None
        for v in range(4):
            ax = _ts_ax(v + 1, f"VAE {v}", vae_colors[v])
            if ax_vae0 is None:
                ax_vae0 = ax
            else:
                ax.sharey(ax_vae0)
            ax.plot(t, vae_det[:, v], lw=0.6, color=vae_colors[v])
            ax.axhline(thr[v], color="#888888", lw=0.8, ls="--")
            if v == 0:
                ax.set_title(
                    "VAE anomaly scores  (dashed = noise mean + 3σ)", fontsize=8)
        if ax_vae0 is not None:
            ax_vae0.set_ylim(0, y_top)

    # ── fallbacks ─────────────────────────────────────────────────────────────

    def _plot_unsupported(self, pm):
        ax = self._fig.add_subplot(111)
        ax.text(0.5, 0.5,
                f"PROC_MODE {pm} ({MODE_LABELS.get(pm, '?')})\n"
                "visualiser not yet implemented",
                ha="center", va="center", fontsize=12, color="#666666")
        ax.axis("off")

    def _plot_error(self, msg):
        ax = self._fig.add_subplot(111)
        ax.text(0.5, 0.5, f"Error loading file:\n{msg}",
                ha="center", va="center", fontsize=10, color="#cc0000",
                wrap=True)
        ax.axis("off")


# ── main browser window ────────────────────────────────────────────────────────

class MicroPAMBrowser(tk.Tk):

    def __init__(self, start_dir: str = "."):
        super().__init__()
        self.title("microPAM File Browser")
        self.geometry("920x540")
        self.minsize(640, 300)

        self._current_dir = Path(start_dir).resolve()
        self._popup:       HeaderPopup | None = None
        self._data_viewer: DataViewer  | None = None

        self._build_ui()
        self._load_directory(self._current_dir)

    # ── UI construction ───────────────────────────────────────────────────────

    def _build_ui(self):
        # toolbar
        toolbar = ttk.Frame(self, padding=(6, 4))
        toolbar.pack(side=tk.TOP, fill=tk.X)

        ttk.Button(toolbar, text="⬆  Up",   command=self._go_up).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="⟳  Refresh", command=self._refresh).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="📂  Open folder…", command=self._browse).pack(side=tk.LEFT, padx=2)

        self._dir_var = tk.StringVar(value=str(self._current_dir))
        dir_entry = ttk.Entry(toolbar, textvariable=self._dir_var, state="readonly", width=60)
        dir_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(8, 2))

        # treeview + scrollbars
        frame = ttk.Frame(self, padding=(6, 0, 6, 6))
        frame.pack(fill=tk.BOTH, expand=True)

        vsb = ttk.Scrollbar(frame, orient=tk.VERTICAL)
        hsb = ttk.Scrollbar(frame, orient=tk.HORIZONTAL)

        self._tree = ttk.Treeview(
            frame, columns=COLUMNS, show="headings",
            yscrollcommand=vsb.set, xscrollcommand=hsb.set,
            selectmode="browse",
        )
        vsb.configure(command=self._tree.yview)
        hsb.configure(command=self._tree.xview)

        col_widths = {COL_NAME: 240, COL_SIZE: 80, COL_DATE: 145,
                      COL_MODE: 160, COL_FSAMP: 80, COL_NCH: 40, COL_DUR: 80}
        for col in COLUMNS:
            self._tree.heading(col, text=col,
                               command=lambda c=col: self._sort_by(c))
            self._tree.column(col, width=col_widths.get(col, 100),
                              anchor=tk.W if col == COL_NAME else tk.CENTER,
                              stretch=(col == COL_NAME))

        self._tree.tag_configure("odd",  background="#eef4fb")
        self._tree.tag_configure("even", background="#ffffff")
        self._tree.tag_configure("err",  foreground="#999999")

        vsb.pack(side=tk.RIGHT,  fill=tk.Y)
        hsb.pack(side=tk.BOTTOM, fill=tk.X)
        self._tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # status bar
        self._status_var = tk.StringVar()
        ttk.Label(self, textvariable=self._status_var, anchor=tk.W,
                  padding=(8, 2)).pack(side=tk.BOTTOM, fill=tk.X)

        # bindings
        self._tree.bind("<Button-3>",        self._on_right_click)
        self._tree.bind("<Double-Button-1>", self._on_double_click)
        self._tree.bind("<Return>",          self._on_return)
        self._tree.bind("v",                 lambda _: self._show_data())

        # context menu
        self._menu = tk.Menu(self, tearoff=0)
        self._menu.add_command(label="Show header info", command=self._show_header)
        self._menu.add_command(label="View data",        command=self._show_data)
        self._menu.add_separator()
        self._menu.add_command(label="Open containing folder",
                               command=self._open_folder)

        # sort state
        self._sort_col = COL_DATE
        self._sort_rev = False

    # ── cursor helpers ────────────────────────────────────────────────────────

    def _set_busy(self, busy: bool):
        cursor = "watch" if busy else ""
        self.configure(cursor=cursor)
        self._tree.configure(cursor=cursor)
        self.update()   # full event-loop flush — ensures cursor is rendered before blocking work

    # ── directory loading ─────────────────────────────────────────────────────

    def _load_directory(self, directory: Path):
        self._set_busy(True)
        try:
            self._current_dir = directory
            self._dir_var.set(str(directory))
            self._tree.delete(*self._tree.get_children())

            files = sorted(
                [p for p in directory.iterdir()
                 if p.is_file() and p.suffix.lower() in EXTENSIONS],
                key=lambda p: p.stat().st_mtime, reverse=True,
            )

            n = len(files)
            self._status_var.set(
                f"Found {n} file{'s' if n != 1 else ''} — decoding headers, please wait…"
            )
            self.update()

            for idx, path in enumerate(files):
                stat = path.stat()
                info = _safe_parse(path)
                if info is None:
                    self._tree.insert("", tk.END, iid=str(path),
                                      values=(path.name, _fmt_size(stat.st_size),
                                              "", "?", "", "", ""),
                                      tags=("err", "odd" if idx % 2 else "even"))
                    continue

                pm    = info.get("proc_mode", -1)
                mtime = datetime.fromtimestamp(stat.st_mtime).strftime("%Y-%m-%d %H:%M:%S")
                self._tree.insert(
                    "", tk.END, iid=str(path),
                    values=(
                        path.name,
                        _fmt_size(stat.st_size),
                        mtime,
                        f"{pm} – {MODE_LABELS.get(pm, '?')}",
                        f"{info.get('sample_rate', '?')} Hz",
                        str(info.get("n_channels", "?")),
                        _fmt_duration(info, path),
                    ),
                    tags=("odd" if idx % 2 else "even",),
                )

            self._status_var.set(
                f"{n} microPAM file{'s' if n != 1 else ''} in {directory}"
            )
        finally:
            self._set_busy(False)

    # ── navigation ────────────────────────────────────────────────────────────

    def _go_up(self):
        parent = self._current_dir.parent
        if parent != self._current_dir:
            self._load_directory(parent)

    def _refresh(self):
        self._load_directory(self._current_dir)

    def _browse(self):
        d = filedialog.askdirectory(initialdir=str(self._current_dir),
                                    title="Select folder")
        if d:
            self._load_directory(Path(d))

    # ── selection helpers ─────────────────────────────────────────────────────

    def _selected_path(self) -> Path | None:
        sel = self._tree.selection()
        return Path(sel[0]) if sel else None

    # ── events ────────────────────────────────────────────────────────────────

    def _on_right_click(self, event):
        row = self._tree.identify_row(event.y)
        if row:
            self._tree.selection_set(row)
            self._menu.tk_popup(event.x_root, event.y_root)

    def _on_double_click(self, event):
        self._show_header()

    def _on_return(self, event):
        self._show_header()

    # ── actions ───────────────────────────────────────────────────────────────

    def _show_header(self):
        path = self._selected_path()
        if path is None:
            return
        self._set_busy(True)
        try:
            info = _safe_parse(path)
        finally:
            self._set_busy(False)
        if info is None:
            messagebox.showerror("Parse error",
                                 f"Could not read WAV header from:\n{path}")
            return
        # only one popup at a time; bring to front if already open for same file
        if self._popup and self._popup.winfo_exists():
            self._popup.destroy()
        self._popup = HeaderPopup(self, path, info)

    def _show_data(self):
        path = self._selected_path()
        if path is None:
            return
        info = _safe_parse(path)
        if info is None:
            messagebox.showerror("Parse error",
                                 f"Could not read header from:\n{path}")
            return
        if self._data_viewer and self._data_viewer.winfo_exists():
            self._data_viewer.destroy()
        self._data_viewer = DataViewer(self, path, info)

    def _open_folder(self):
        path = self._selected_path()
        if path is None:
            return
        folder = str(path.parent)
        if sys.platform == "win32":
            os.startfile(folder)
        elif sys.platform == "darwin":
            os.system(f'open "{folder}"')
        else:
            os.system(f'xdg-open "{folder}"')

    # ── column sorting ────────────────────────────────────────────────────────

    def _sort_by(self, col: str):
        rows = [(self._tree.set(iid, col), iid)
                for iid in self._tree.get_children()]
        rev = (col == self._sort_col) and not self._sort_rev
        rows.sort(reverse=rev,
                  key=lambda x: (x[0].lower() if isinstance(x[0], str) else x[0]))
        for i, (_, iid) in enumerate(rows):
            self._tree.move(iid, "", i)
            tags = list(self._tree.item(iid, "tags"))
            tags = [t for t in tags if t not in ("odd", "even")]
            tags.append("odd" if i % 2 else "even")
            self._tree.item(iid, tags=tags)
        self._sort_col = col
        self._sort_rev = rev


# ── entry point ────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    start = sys.argv[1] if len(sys.argv) > 1 else str(Path.cwd())
    app = MicroPAMBrowser(start_dir=start)
    app.mainloop()
