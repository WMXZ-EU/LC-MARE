"""
micropam_browser.py — microPAM_V5 file browser
================================================
Lists microPAM_V5 data files (.wav / .bin) in a chosen directory.  Right-click
on any file to open a popup with the parsed header.

V5 firmware writes only PROC_MODE 0 (raw WAV) and 1 (compressed raw); the
spectrum/intensity/VAE modes and their .spc/.int/.vae files are V4x-only and are
read by the V4x tooling's own decoder, not by this browser or microPAM.reader.

The ⚙ config.txt button edits the SD card's configuration file. The firmware
reads it from the card root only (`/config.txt`), so the button always targets
the root of the volume being browsed (D:/ while in D:/E50BA842_20260727/11),
never the folder in view. It is disabled unless a card is actually in view —
that is, unless the root already has a config.txt or the folder holds
recordings — so it can never write a config.txt to an unrelated drive root such
as C:/. It offers the same fields as the serial Configurator
(micropam_control.py), sharing one format implementation in micropam_config.py.
Fields there are in the file's own units: fsamp is in **kHz**, not the Hz shown
for recordings in the file list and header popup.

Usage
-----
    python Python/micropam_browser.py [start_directory]
"""

import sys
import os
import tkinter as tk
import tkinter.font as tkfont
from contextlib import contextmanager
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from datetime import datetime

import numpy as np

# reader.py (copy of microPAM_V5 microPAM/reader.py) lives in the same directory.
sys.path.insert(0, str(Path(__file__).parent))
from reader import (MicroPAMFile, _parse_wav_header,
                             recover_stream_info, HEADER_BYTES)
# pre-3.3.0 (adalogger_V3) IKEY and compressed-record layout
from legacy_reader import fix_legacy_info, read_legacy
# acoustic calibration from the ISRC suffix, as pamlib.io.micropam_calib
from micropam_calib import parse_isrc, count_level_db
# config.txt format, shared with micropam_control.py
from micropam_config import (CONFIG_FILENAME, CONFIG_LINES, CONFIG_KEYS,
                             CONFIG_DEFAULTS, read_config_file,
                             write_config_file, validate_config_value)

# ── constants ──────────────────────────────────────────────────────────────────
EXTENSIONS   = {".wav", ".bin"}
MODE_LABELS  = {0: "WAV (raw PCM)", 1: "BIN (compressed)"}

COL_NAME  = "File"
COL_SIZE  = "Size"
COL_DATE  = "Modified"
COL_MODE  = "Mode"
COL_FSAMP = "fsamp"
COL_NCH   = "nch"
COL_DUR   = "Dur / Size"

COLUMNS = (COL_NAME, COL_SIZE, COL_DATE, COL_MODE, COL_FSAMP, COL_NCH, COL_DUR)


# ── helpers ────────────────────────────────────────────────────────────────────

@contextmanager
def busy(*windows):
    """Watch cursor on the given windows and all their widgets while the block runs.
    A child widget does not inherit a cursor set on its toplevel (the file list and the
    matplotlib canvas keep their own), so each widget is set and restored individually."""
    saved = []

    def walk(w):
        try:
            saved.append((w, w.cget("cursor")))
            w.configure(cursor="watch")
        except tk.TclError:
            pass
        for child in w.winfo_children():
            walk(child)

    for w in windows:
        walk(w)
    windows[0].update()     # full flush, or the cursor is drawn only after the work
    try:
        yield
    finally:
        for w, cursor in reversed(saved):   # a widget met twice gets its first cursor back
            try:
                w.configure(cursor=cursor)
            except tk.TclError:     # destroyed meanwhile
                pass


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
    nbytes = path.stat().st_size - info.get("header_bytes", HEADER_BYTES)
    if pm == 0 and fs > 0 and nch > 0:
        secs = nbytes / (nch * 4 * fs)
        if secs > 0:
            m, s = divmod(int(secs), 60)
            h, m = divmod(m, 60)
            return f"{h:02d}:{m:02d}:{s:02d}" if h else f"{m:02d}:{s:02d}"
    return _fmt_size(nbytes)


def _safe_parse(path: Path) -> dict | None:
    try:
        # read past the baseline 512 so a 1024-byte (USE_DCL) header parses fully
        with open(path, "rb") as f:
            raw = f.read(8192)
        if len(raw) < HEADER_BYTES:
            return None
        info = _parse_wav_header(raw)
        if not info["ikey_ok"]:
            # never closed cleanly - recover the mode from the stream, or the
            # list would call a compressed file raw (see recover_stream_info)
            recover_stream_info(path, info)
        return fix_legacy_info(path, info)
    except Exception:
        return None


def load_samples(path: Path, info: dict) -> dict:
    """Decode a whole file, V5 or legacy; result["data"] is (N, nch) int32."""
    if info.get("legacy") and info.get("proc_mode") == 1:
        return read_legacy(path, info)
    return MicroPAMFile(path).read_all()


def welch_psd(x: np.ndarray, fs: float, nperseg: int, overlap: float = 0.5,
              chunk: int = 256) -> tuple:
    """One-sided power spectral density by Welch's method: Hann window, segments
    overlapping by `overlap`, mean removed per segment, periodograms averaged.
    Segments are processed `chunk` at a time, so a one-minute file at 96 kHz does
    not need a (nseg, nperseg) array in memory. Returns (f, psd, nseg); psd is in
    x-units squared per Hz."""
    step = max(1, int(nperseg * (1 - overlap)))
    nseg = (len(x) - nperseg) // step + 1 if len(x) >= nperseg else 0
    f = np.fft.rfftfreq(nperseg, 1 / fs)
    if nseg < 1:
        return f, np.full(len(f), np.nan), 0
    win = np.hanning(nperseg)
    acc = np.zeros(len(f))
    segs = np.lib.stride_tricks.sliding_window_view(x, nperseg)[::step][:nseg]
    for k in range(0, nseg, chunk):
        s = segs[k:k + chunk].astype(np.float64)
        s -= s.mean(axis=1, keepdims=True)
        acc += (np.abs(np.fft.rfft(s * win, axis=1)) ** 2).sum(axis=0)
    psd = acc / (nseg * fs * (win ** 2).sum())
    psd[1:-1 if nperseg % 2 == 0 else None] *= 2      # fold negative frequencies
    return f, psd, nseg


def mode_text(info: dict) -> str:
    """Mode column text: PROC_MODE, its label, and whether it is legacy or recovered."""
    pm = info.get("proc_mode", -1)
    return (f"{pm} – {MODE_LABELS.get(pm, '?')}"
            + (" (legacy)" if info.get("legacy") else "")
            + (" (recovered)" if info.get("recovered") else ""))


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

        txt = tk.Text(frame, wrap=tk.NONE, font="TkFixedFont",
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
        self._head_font = tkfont.nametofont("TkFixedFont").copy()   # bold, at the current size
        self._head_font.configure(weight="bold")
        txt.tag_configure("head",  foreground="#006000", font=self._head_font)
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
        if info.get("recovered"):
            add("IKEY", "missing — file not closed cleanly; PROC_MODE and "
                        "NBUF_PROC recovered from the data stream, the rest "
                        "is unrecoverable")
        pm = info.get("proc_mode", -1)
        add("PROC_MODE",   f"{pm}  —  {MODE_LABELS.get(pm, 'unknown')}"
                           f"{'  (recovered)' if info.get('recovered') else ''}")
        if info.get("legacy"):
            add("Format",  "legacy V3 (firmware before 3.3.0): own IKEY and record header")
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

        sep("Calibration (ISRC)")
        name, c0 = parse_isrc(info.get("chunks", {}).get("ISRC", ""))
        if c0 is None:
            add("Source", f"{name}  (no calibration: ISRC is not <name>-<C0>)")
        else:
            add("Source", name)
            add("C0 (dB re 1 µPa)", f"{c0:.1f}  per 24-bit count at 0 dB gain")
            add("1 count (dB re 1 µPa)", f"{count_level_db(info):.1f}  "
                                          "at this gain and SHIFT")

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
                     "Run: pip install matplotlib", font="TkFixedFont",
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

        # colour range of the spectrogram: cmin/cmax in CLIM_STEP dB steps, Reset
        # returns to the initial range (peak - 60 dB .. peak)
        self._im = None
        self._clim0 = None
        # packed ahead of the figure frame: the figure asks for more height than the
        # window has, and pack drops the last-packed widget
        bar = ttk.Frame(self, padding=(8, 0, 8, 6))
        bar.pack(side=tk.BOTTOM, fill=tk.X, before=frame)
        self._clim_btns = []
        for label, which in (("cmin", 0), ("cmax", 1)):
            ttk.Label(bar, text=label).pack(side=tk.LEFT, padx=(0, 2))
            for arrow, sign in (("▼", -1), ("▲", +1)):
                b = ttk.Button(bar, text=arrow, width=2,
                               command=lambda w=which, s=sign: self._step_clim(w, s))
                b.pack(side=tk.LEFT)
                self._clim_btns.append(b)
            ttk.Frame(bar, width=10).pack(side=tk.LEFT)
        b = ttk.Button(bar, text="Reset", command=self._reset_clim)
        b.pack(side=tk.LEFT)
        self._clim_btns.append(b)
        self._clim_var = tk.StringVar()
        ttk.Label(bar, textvariable=self._clim_var, foreground="#555555").pack(
            side=tk.LEFT, padx=8)
        ttk.Button(bar, text="Close", command=self.destroy).pack(side=tk.RIGHT)

        self._load_and_plot()
        self._show_clim()

    # ── data loading ──────────────────────────────────────────────────────────

    def _load_and_plot(self):
        with busy(self, self.master):
            try:
                result = load_samples(self._path, self._info)
                pm = self._pm
                if pm in (0, 1):
                    self._plot_mode01(result)
                else:
                    self._plot_unsupported(pm)
            except Exception as exc:
                self._plot_error(str(exc))
            finally:
                self._canvas.draw()

    # ── layout: modes 0 and 1 ────────────────────────────────────────────────

    def _plot_mode01(self, result):
        data = result["data"]                  # (nframes, nch) int32
        fs   = self._info["sample_rate"]
        nch  = data.shape[1]
        t    = np.arange(data.shape[0]) / fs
        cal  = count_level_db(self._info)       # dB re 1 µPa of one count, or None
        scale = 1.0 if cal is None else 10 ** (cal / 20) * 1e-6   # count -> Pa

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
            ax.plot(t, data[:, ch] * scale, lw=0.4,
                    color=self._CHAN_COLORS[ch % len(self._CHAN_COLORS)])
            ax.set_ylabel(f"Ch{ch}", fontsize=7, rotation=0, labelpad=22)
            ax.tick_params(labelsize=6)
            ax.margins(x=0)
            if ch == 0:
                ax.set_title("Time series (counts)" if cal is None else
                             "Time series (Pa)", fontsize=8)
            if ch < nch - 1:
                ax.set_xticklabels([])
            else:
                ax.set_xlabel("Time (s)", fontsize=7)

        # ── spectrogram (col 0, last row) + colorbar (col 1, last row) ───────
        ax_spc = self._fig.add_subplot(gs[nch, 0], sharex=ax0)
        cax    = self._fig.add_subplot(gs[nch, 1])
        im = self._draw_spectrogram(ax_spc, data[:, 0].astype(np.float64),
                                    fs, title="Spectrogram  ch0", cal=cal)
        if im is not None:
            cb = self._fig.colorbar(im, cax=cax)
            cb.set_label("dB re 1 count²/Hz" if cal is None else "dB re 1 µPa²/Hz",
                         fontsize=6)
            cb.ax.tick_params(labelsize=6)
            self._im = im
            self._clim0 = im.get_clim()

    # ── colour range ──────────────────────────────────────────────────────────

    CLIM_STEP = 5       # dB per arrow click

    def _step_clim(self, which, sign):
        if self._im is None:
            return
        clim = list(self._im.get_clim())
        clim[which] += sign * self.CLIM_STEP
        if clim[0] >= clim[1]:          # keep at least one step between cmin and cmax
            return
        self._im.set_clim(*clim)
        self._show_clim()
        self._canvas.draw_idle()

    def _reset_clim(self):
        if self._im is None:
            return
        self._im.set_clim(*self._clim0)
        self._show_clim()
        self._canvas.draw_idle()

    def _show_clim(self):
        state = ["!disabled"] if self._im is not None else ["disabled"]
        for b in self._clim_btns:
            b.state(state)
        if self._im is None:
            self._clim_var.set("")
        else:
            lo, hi = self._im.get_clim()
            self._clim_var.set(f"{lo:.0f} … {hi:.0f} dB")

    # ── spectrogram helper ────────────────────────────────────────────────────

    def _draw_spectrogram(self, ax, sig, fs, *, title="", nperseg=512, noverlap=256,
                          cal=None):
        """Spectrogram as power spectral density, dB re 1 count²/Hz, or dB re
        1 µPa²/Hz when `cal` (dB re 1 µPa of one count) is given."""
        step   = nperseg - noverlap
        nsteps = max(1, (len(sig) - nperseg) // step)
        if nsteps < 2:
            ax.set_title(title + " (insufficient data)", fontsize=8)
            return None
        window = np.hanning(nperseg)
        frames = np.stack(
            [sig[i * step: i * step + nperseg] * window for i in range(nsteps)]
        )                                                    # (nsteps, nperseg)
        psd     = np.abs(np.fft.rfft(frames, axis=1)) ** 2                   * (2 / (fs * (window ** 2).sum()))         # (nsteps, nperseg//2+1)
        spec_db = 10 * np.log10(psd + 1e-20) + (cal or 0.0)

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

    # ── fallbacks ─────────────────────────────────────────────────────────────

    def _plot_unsupported(self, pm):
        ax = self._fig.add_subplot(111)
        ax.text(0.5, 0.5,
                f"PROC_MODE {pm} ({MODE_LABELS.get(pm, '?')})\n"
                "is not a microPAM_V5 mode — read V4x files\n"
                "with the V4x project's own decoder",
                ha="center", va="center", fontsize=12, color="#666666")
        ax.axis("off")

    def _plot_error(self, msg):
        ax = self._fig.add_subplot(111)
        ax.text(0.5, 0.5, f"Error loading file:\n{msg}",
                ha="center", va="center", fontsize=10, color="#cc0000",
                wrap=True)
        ax.axis("off")


# ── Welch spectrum window ─────────────────────────────────────────────────────

class SpectrumViewer(tk.Toplevel):
    """Welch power spectral density of one file, all channels overlaid. The file is
    decoded once; segment length and frequency axis can be changed afterwards.
    Samples are ADC counts after SHIFT, so the level is in dB re 1 count²/Hz, or in
    dB re 1 µPa²/Hz when ISRC carries a calibration (micropam_calib.py)."""

    NPERSEG = (512, 1024, 2048, 4096, 8192, 16384, 32768)

    def __init__(self, parent, path: Path, info: dict):
        super().__init__(parent)
        self.title(f"Spectrum — {path.name}")
        self.geometry("980x620")
        self.minsize(560, 360)
        self.bind("<Escape>", lambda _: self.destroy())
        self._path, self._info = path, info
        self._data = None
        self._cal  = count_level_db(info)       # dB re 1 µPa of one count, or None

        try:
            from matplotlib.backends.backend_tkagg import (
                FigureCanvasTkAgg, NavigationToolbar2Tk)
            import matplotlib.figure as mplf
        except ImportError:
            tk.Label(self, text="matplotlib is not installed.\n"
                     "Run: pip install matplotlib", font="TkFixedFont",
                     fg="#cc0000").pack(expand=True)
            ttk.Button(self, text="Close", command=self.destroy).pack(pady=6)
            return

        bar = ttk.Frame(self, padding=(8, 6, 8, 0))
        bar.pack(fill=tk.X)
        ttk.Label(bar, text="Segment length").pack(side=tk.LEFT)
        self.nperseg_var = tk.StringVar(value="4096")
        box = ttk.Combobox(bar, textvariable=self.nperseg_var, state="readonly", width=7,
                           values=[str(n) for n in self.NPERSEG])
        box.pack(side=tk.LEFT, padx=(4, 12))
        box.bind("<<ComboboxSelected>>", lambda _: self._plot())
        self.logf_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(bar, text="Log frequency axis", variable=self.logf_var,
                        command=self._plot).pack(side=tk.LEFT)
        self.status_var = tk.StringVar()
        ttk.Label(bar, textvariable=self.status_var, foreground="#555555").pack(
            side=tk.LEFT, padx=12)

        self._fig = mplf.Figure(figsize=(11, 6), dpi=96, facecolor="#f8f8f8",
                                layout="constrained")
        frame = ttk.Frame(self)
        frame.pack(fill=tk.BOTH, expand=True)
        self._canvas = FigureCanvasTkAgg(self._fig, master=frame)
        NavigationToolbar2Tk(self._canvas, frame).update()
        self._canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        ttk.Button(self, text="Close", command=self.destroy).pack(side=tk.BOTTOM, pady=(0, 6),
                                                                    before=frame)

        with busy(self, self.master):
            try:
                self._data = load_samples(path, info)["data"]
            except Exception as exc:
                self._message(f"Error loading file:\n{exc}", "#cc0000")
            if self._data is not None:
                self._plot()

    def _message(self, text, color="#666666"):
        self._fig.clear()
        ax = self._fig.add_subplot(111)
        ax.text(0.5, 0.5, text, ha="center", va="center", fontsize=11, color=color, wrap=True)
        ax.axis("off")
        self._canvas.draw()

    def _plot(self):
        if self._data is None:
            return
        fs = self._info["sample_rate"]
        nperseg = int(self.nperseg_var.get())
        if len(self._data) < nperseg:
            self._message(f"{len(self._data)} samples: too few for a {nperseg}-point segment")
            return
        with busy(self):
            self._plot_welch(fs, nperseg)

    def _plot_welch(self, fs, nperseg):
        self._fig.clear()
        ax = self._fig.add_subplot(111)
        nseg = 0
        for ch in range(self._data.shape[1]):
            f, psd, nseg = welch_psd(self._data[:, ch], fs, nperseg)
            sel = slice(1, None) if self.logf_var.get() else slice(None)   # no DC on log axis
            ax.plot(f[sel], 10 * np.log10(psd[sel] + 1e-20) + (self._cal or 0.0), lw=0.8,
                    color=DataViewer._CHAN_COLORS[ch % len(DataViewer._CHAN_COLORS)],
                    label=f"Ch{ch}")
        ax.set_xscale("log" if self.logf_var.get() else "linear")
        ax.set_xlim(f[1] if self.logf_var.get() else 0, fs / 2)
        ax.set_xlabel("Frequency (Hz)", fontsize=8)
        ax.set_ylabel("PSD (dB re 1 count²/Hz)" if self._cal is None else
                      "PSD (dB re 1 µPa²/Hz)", fontsize=8)
        ax.set_title(f"{self._path.name}   Welch: Hann, {nperseg} points, 50 % overlap, "
                     f"{nseg} segments averaged", fontsize=8)
        ax.tick_params(labelsize=7)
        ax.grid(True, which="both", lw=0.3, alpha=0.6)
        if self._data.shape[1] > 1:
            ax.legend(fontsize=7)
        self.status_var.set(f"fs {fs} Hz   Δf {fs / nperseg:.1f} Hz   "
                            f"{len(self._data) / fs:.1f} s   "
                            + ("uncalibrated" if self._cal is None else
                               f"1 count = {self._cal:.1f} dB re 1 µPa"))
        self._canvas.draw()


# ── config.txt editor window ──────────────────────────────────────────────────

class ConfigEditor(tk.Toplevel):
    """Edit the `config.txt` sitting next to the recordings.

    The firmware reads this file off the SD card at boot and rewrites it on
    stop (src/filing.cxx), so editing it here is how a card is configured
    without a serial connection. Fields are in file order and file units —
    note fsamp is in **kHz** here, unlike the Hz shown for recordings
    elsewhere in this browser. See micropam_config.py for the format.
    """

    def __init__(self, parent, path: Path):
        super().__init__(parent)
        self.path = path
        self.title(f"config.txt — {path.parent}")
        self.resizable(False, False)
        self.bind("<Escape>", lambda _: self.destroy())

        frame = ttk.Frame(self, padding=10)
        frame.pack(fill=tk.BOTH, expand=True)

        ttk.Label(frame, text=str(path), foreground="#0050a0").grid(
            row=0, column=0, columnspan=3, sticky="w", pady=(0, 8))

        self.vars = {}
        for i, (key, _sep, comment) in enumerate(CONFIG_LINES, start=1):
            ttk.Label(frame, text=f"!{key}").grid(row=i, column=0, sticky="e",
                                                  padx=(0, 6), pady=2)
            v = tk.StringVar()
            self.vars[key] = v
            ttk.Entry(frame, textvariable=v, width=26).grid(row=i, column=1,
                                                            pady=2)
            ttk.Label(frame, text=comment, foreground="#666666").grid(
                row=i, column=2, sticky="w", padx=(8, 0), pady=2)

        self.status_var = tk.StringVar()
        ttk.Label(frame, textvariable=self.status_var).grid(
            row=len(CONFIG_LINES) + 1, column=0, columnspan=3, sticky="w",
            pady=(8, 4))

        bar = ttk.Frame(frame)
        bar.grid(row=len(CONFIG_LINES) + 2, column=0, columnspan=3, sticky="e")
        ttk.Button(bar, text="Reload", command=self._load).pack(side="left", padx=4)
        ttk.Button(bar, text="Save",   command=self._save).pack(side="left", padx=4)
        ttk.Button(bar, text="Close",  command=self.destroy).pack(side="left", padx=4)

        self._load()

        self.update_idletasks()
        px = parent.winfo_rootx() + (parent.winfo_width()  - self.winfo_width())  // 2
        py = parent.winfo_rooty() + (parent.winfo_height() - self.winfo_height()) // 2
        self.geometry(f"+{max(0, px)}+{max(0, py)}")
        self.focus_set()

    def _load(self):
        if not self.path.exists():
            self.status_var.set("No config.txt here yet — Save creates one.")
            return
        try:
            found = read_config_file(self.path)
        except OSError as e:
            self.status_var.set(f"Could not read: {e}")
            return
        for key in CONFIG_KEYS:
            self.vars[key].set(found.get(key, CONFIG_DEFAULTS.get(key, "")))
        missing  = [k for k in CONFIG_KEYS if k not in found]
        defaults = [k for k in missing if k in CONFIG_DEFAULTS]
        missing  = [k for k in missing if k not in CONFIG_DEFAULTS]
        note = ""
        if defaults:
            note += "; defaulted " + " ".join(
                f"!{k}={CONFIG_DEFAULTS[k]}" for k in defaults)
        if missing:
            note += "; missing " + " ".join(missing)
        self.status_var.set(
            f"Loaded {len(found)}/{len(CONFIG_KEYS)} keys" + note)

    def _save(self):
        values, errors = {}, []
        for key in CONFIG_KEYS:
            val = self.vars[key].get().strip()
            err = validate_config_value(key, val)
            if err:
                errors.append(err)
            else:
                values[key] = val
        if errors:
            messagebox.showerror("Cannot save config.txt",
                                 "Fix these fields first:\n\n  "
                                 + "\n  ".join(errors), parent=self)
            return
        try:
            write_config_file(self.path, values)
        except (OSError, UnicodeEncodeError) as e:
            messagebox.showerror("Save failed", str(e), parent=self)
            return
        self.status_var.set(f"Saved {datetime.now().strftime('%H:%M:%S')}")


# ── main browser window ────────────────────────────────────────────────────────

class MicroPAMBrowser(tk.Tk):

    def __init__(self, start_dir: str = "."):
        super().__init__()
        self.title("microPAM File Browser")
        self.geometry("920x540")
        self.minsize(640, 300)

        self._current_dir = Path(start_dir).resolve()
        self._popup:       HeaderPopup  | None = None
        self._data_viewer: DataViewer   | None = None
        self._cfg_editor:  ConfigEditor | None = None

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

        # config.txt of the card root — label and enabled state both come from
        # _load_directory(); disabled until a card volume is actually in view.
        self._cfg_btn_var = tk.StringVar(value=f"⚙  {CONFIG_FILENAME}")
        self._cfg_btn = ttk.Button(toolbar, textvariable=self._cfg_btn_var,
                                   command=self._show_config, state=tk.DISABLED)
        self._cfg_btn.pack(side=tk.LEFT, padx=2)

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
                        mode_text(info),
                        f"{info.get('sample_rate', '?')} Hz",
                        str(info.get("n_channels", "?")),
                        _fmt_duration(info, path),
                    ),
                    tags=("odd" if idx % 2 else "even",),
                )

            # The button targets the card root, so it only makes sense on a card:
            # either the root already holds a config.txt, or this folder holds
            # recordings (then Save creates the config at that root). Neither ->
            # no data volume in view (the start-up folder, say), so disable it
            # rather than offer to write config.txt to some unrelated drive root.
            cfg      = self._config_path()
            have_cfg = cfg.is_file()
            on_card  = have_cfg or n > 0
            self._cfg_btn_var.set(
                f"⚙  {CONFIG_FILENAME}"
                + ("" if have_cfg else " (new)" if on_card else " (no card)"))
            self._cfg_btn.configure(state=tk.NORMAL if on_card else tk.DISABLED)
            self._status_var.set(
                f"{n} microPAM file{'s' if n != 1 else ''} in {directory}"
                + (f" · {CONFIG_FILENAME} at {cfg.parent}" if have_cfg else "")
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

    def _config_path(self) -> Path:
        """Where config.txt lives for the volume being browsed. The firmware
        only ever reads `/config.txt` off the card root, so this is the root of
        the current path (D:\\ for D:\\E50BA842_20260727\\11), not the folder in
        view. Falls back to the current folder if the path has no anchor."""
        anchor = self._current_dir.anchor
        return (Path(anchor) if anchor else self._current_dir) / CONFIG_FILENAME

    def _show_config(self):
        """Open the card root's config.txt. Opens empty if there is none — Save
        then creates it, at the root."""
        if self._cfg_editor and self._cfg_editor.winfo_exists():
            self._cfg_editor.destroy()
        self._cfg_editor = ConfigEditor(self, self._config_path())

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
