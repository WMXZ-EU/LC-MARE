"""
workbench.py — microPAM_V3 local workbench
==========================================

One window for the bench work around a microPAM_V3 recorder:

  Firmware       developer mode: choose target and PROC_MODE, build / upload / flash via
                 scripts/*.ps1; user mode: upload a ready-built .uf2 file (uf2_upload.py),
                 no compiler needed
  Configuration  every config.txt parameter; load/save a local file, read/write the board
  Device         serial connection, start/stop, clock, hibernate, serial monitor
  Data           browse local folders, header info and plots of .wav/.bin recordings

Built from the microPAM_V5 Python tools: reader.py (V5 reader, unchanged),
micropam_config.py (config.txt format) and micropam_browser.py (header popup,
data viewer). microPAM_V3 firmware 3.3.0 writes the V5 file format, so the V5
reader decodes its files.

config.txt keys versus serial-menu keys
---------------------------------------
The file and the menu use the same letters except
  p   file: IPRD (project)      menu: k   (menu p prints PROC_MODE)
  f   file: kHz                 menu: Hz
  m   PROC_MODE: compile-time; written to the file, never set on the board
Writing the configuration to the board sends one !<key><value> per parameter
and then 'w', which stores /config.txt on the SD card at once.

Usage
-----
    Python\\.venv\\Scripts\\python.exe Python\\workbench.py [--dev]

The Firmware tab starts in user mode (upload a .uf2 file); --dev starts it in developer
mode (build from source), which needs config.h, the scripts and Arduino (arduino-cli
with the rp2040 core, see find_arduino); without Arduino developer mode is disabled
until "Check again" finds it.
"""

import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import tkinter as tk
import tkinter.font as tkfont
from datetime import datetime, timezone
from pathlib import Path
from tkinter import ttk, filedialog, messagebox, scrolledtext

import serial
import serial.tools.list_ports

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from micropam_config import (CONFIG_FILENAME, CONFIG_LINES, CONFIG_KEYS,      # noqa: E402
                             CONFIG_DEFAULTS, read_config_file,
                             write_config_file, validate_config_value)
import bin2wav                                                                              # noqa: E402
from micropam_browser import (HeaderPopup, DataViewer, SpectrumViewer, EXTENSIONS, busy,     # noqa: E402
                              MODE_LABELS, _safe_parse, _fmt_size, _fmt_duration,
                              mode_text)
from uf2_upload import uf2_info, upload as upload_uf2, UploadError            # noqa: E402

ROOT      = HERE.parent                      # firmware project folder
SCRIPTS   = ROOT / "scripts"
CONFIG_H  = ROOT / "config.h"
GLOBAL_H  = ROOT / "src" / "global.h"
SETTINGS  = HERE / "workbench_settings.json"
TARGETS   = ("rp2040", "rp2350")
BAUD      = 115200
FONT_SIZE = (8, 11, 20)                       # text size in points: minimum, default, maximum
UI_FONTS  = ("TkDefaultFont", "TkTextFont", "TkMenuFont", "TkHeadingFont", "TkCaptionFont",
             "TkSmallCaptionFont", "TkIconFont", "TkTooltipFont", "TkFixedFont")

# serial reply key -> config.txt key (where they differ)
SERIAL_TO_CFG = {"k": "p"}
CFG_TO_SERIAL = {"p": "k"}
CFG_ONLY      = {"m"}                         # not settable on the board
TIME_RE       = re.compile(r"^\d{4}-\d{2}-\d{2}_\d{2}:\d{2}:\d{2}$")


# ── settings ──────────────────────────────────────────────────────────────────

def load_settings() -> dict:
    try:
        return json.loads(SETTINGS.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_settings(values: dict):
    try:
        SETTINGS.write_text(json.dumps(values, indent=2), encoding="utf-8")
    except OSError:
        pass


# ── Arduino ───────────────────────────────────────────────────────────────────

def find_arduino():
    """(arduino-cli path, None) if firmware can be built here, else (path or None,
    reason). Searches the places scripts/common.ps1 (Find-ArduinoCli) searches."""
    local = Path(os.environ.get("LOCALAPPDATA", ""))
    ide = Path("resources/app/lib/backend/resources/arduino-cli.exe")
    candidates = [os.environ.get("ARDUINO_CLI"),
                  local / "Programs/Arduino IDE" / ide,
                  Path(os.environ.get("ProgramFiles", "")) / "Arduino IDE" / ide,
                  shutil.which("arduino-cli")]
    cli = next((Path(c) for c in candidates if c and Path(c).is_file()), None)
    if cli is None:
        return None, ("Arduino not found — install Arduino IDE 2 (or arduino-cli) and its "
                      "board package 'Raspberry Pi Pico/RP2040/RP2350' by Earle F. Philhower "
                      "to build firmware from source.")
    if not (local / "Arduino15/packages/rp2040/hardware/rp2040").is_dir():
        return cli, (f"Arduino found ({cli}), but the board package 'Raspberry Pi "
                     "Pico/RP2040/RP2350' by Earle F. Philhower is not installed.")
    return cli, None


# ── config.h ──────────────────────────────────────────────────────────────────

def _define(text: str, name: str):
    m = re.search(rf'^\s*#define\s+{name}\s+("([^"]*)"|(\d+))', text, re.M)
    if not m:
        return None
    return m.group(2) if m.group(2) is not None else int(m.group(3))


def config_h_defaults() -> dict:
    """config.txt values equivalent to the compile-time defaults in config.h."""
    text = CONFIG_H.read_text(encoding="utf-8")
    preamp = _define(GLOBAL_H.read_text(encoding="utf-8"), "PREAMP") or 0
    return {
        "a": _define(text, "T_ACQ"), "o": _define(text, "T_ON"), "r": _define(text, "T_REP"),
        "f": (_define(text, "FSAMP") or 192000) // 1000,
        "g": 20 if preamp == 0 else 0,
        "m": _define(text, "PROC_MODE"),
        "s": _define(text, "SRC_str"), "c": _define(text, "CMS_str"),
        "n": _define(text, "ART_str"), "p": _define(text, "PRD_str"),
        "e": _define(text, "SBJ_str"), "l": _define(text, "NAM_str"),
        "1": 0, "2": 12, "3": 12, "4": 24,
        "x": "2000-01-01_00:00:00",
    }


def get_proc_mode() -> int:
    return _define(CONFIG_H.read_text(encoding="utf-8"), "PROC_MODE")


def set_proc_mode(mode: int):
    text = CONFIG_H.read_text(encoding="utf-8")
    new, n = re.subn(r"^(\s*#define\s+PROC_MODE\s+)\d+", rf"\g<1>{mode}", text, count=1, flags=re.M)
    if n != 1:
        raise RuntimeError("PROC_MODE #define not found in config.h")
    CONFIG_H.write_text(new, encoding="utf-8", newline="")


# ── serial link ───────────────────────────────────────────────────────────────

class Board:
    """Serial connection to the recorder; received lines go to `rx` as ("rx", line)."""

    def __init__(self, rx: queue.Queue):
        self.rx = rx
        self.ser = None
        self.lock = threading.Lock()

    @property
    def is_open(self) -> bool:
        return self.ser is not None and self.ser.is_open

    def connect(self, port: str):
        self.ser = serial.Serial(port, BAUD, timeout=0.1)
        self.ser.dtr = True
        threading.Thread(target=self._reader, daemon=True).start()

    def disconnect(self):
        ser, self.ser = self.ser, None
        if ser:
            try:
                ser.close()
            except serial.SerialException:
                pass

    def write(self, text: str):
        with self.lock:
            if not self.is_open:
                raise serial.SerialException("not connected")
            self.ser.write(text.encode("ascii", errors="replace"))

    def _reader(self):
        ser = self.ser
        while ser is not None and ser.is_open:
            try:
                line = ser.readline()
            except Exception:                   # port closed under us, or the device vanished
                break
            if line:
                self.rx.put(("rx", line.decode("utf-8", errors="replace")))
        self.rx.put(("lost", ser))


# ── main window ───────────────────────────────────────────────────────────────

class Workbench(tk.Tk):

    def __init__(self, user_mode: bool = True):
        super().__init__()
        self.title("microPAM_V3 Workbench")
        self.user_mode = user_mode
        self.geometry("1100x760")
        self.minsize(900, 600)

        self.settings = load_settings()
        self._init_fonts()
        self.events = queue.Queue()           # ("rx", line) | ("fw", line) | ("fw_done", rc) | ...
        self.board = Board(self.events)
        self.proc = None                       # running firmware script

        self._build_connection_bar()
        nb = ttk.Notebook(self)
        nb.pack(fill=tk.BOTH, expand=True, padx=6, pady=(0, 6))
        self.firmware = FirmwareTab(nb, self)
        self.config_tab = ConfigTab(nb, self)
        self.device = DeviceTab(nb, self)
        self.data = DataTab(nb, self)
        nb.add(self.firmware, text="  Firmware  ")
        nb.add(self.config_tab, text="  Configuration  ")
        nb.add(self.device, text="  Device  ")
        nb.add(self.data, text="  Data  ")
        nb.select(self.settings.get("tab", 0))
        self.nb = nb
        self._fit_window()

        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self._poll_id = self.after(50, self._poll)

    # ── text size ─────────────────────────────────────────────────────────────

    def _init_fonts(self):
        """All widgets use Tk's named fonts (fixed text: TkFixedFont, bold: WbFixedBold),
        so one size setting scales the window; Ctrl +/- change it, Ctrl 0 resets it."""
        tkfont.nametofont("TkFixedFont").configure(family="Consolas")
        self.fixed_bold = tkfont.Font(self, name="WbFixedBold", family="Consolas", weight="bold")
        self.set_font_size(self.settings.get("font_size", FONT_SIZE[1]))
        for seq, step in (("<Control-plus>", 1), ("<Control-equal>", 1), ("<Control-KP_Add>", 1),
                          ("<Control-minus>", -1), ("<Control-KP_Subtract>", -1)):
            self.bind_all(seq, lambda _e, s=step: self.set_font_size(self.font_size + s))
        self.bind_all("<Control-0>", lambda _e: self.set_font_size(FONT_SIZE[1]))

    def set_font_size(self, size):
        try:
            size = int(size)
        except (TypeError, ValueError):
            size = FONT_SIZE[1]
        size = max(FONT_SIZE[0], min(FONT_SIZE[2], size))
        for name in UI_FONTS:
            tkfont.nametofont(name).configure(size=size)
        self.fixed_bold.configure(size=size)
        line = tkfont.nametofont("TkDefaultFont").metrics("linespace")
        ttk.Style(self).configure("Treeview", rowheight=int(line * 1.25))
        self.font_size = self.settings["font_size"] = size
        if hasattr(self, "nb"):
            self._fit_window()

    def _fit_window(self):
        """Grow the window to the size its widgets request at the current text size
        (never beyond the screen, never shrinking what the user has set)."""
        self.update_idletasks()
        w = min(max(self.winfo_width(), self.winfo_reqwidth()), self.winfo_screenwidth() - 40)
        h = min(max(self.winfo_height(), self.winfo_reqheight()), self.winfo_screenheight() - 80)
        if (w, h) != (self.winfo_width(), self.winfo_height()):
            self.geometry(f"{w}x{h}")

    # ── connection bar ────────────────────────────────────────────────────────

    def _build_connection_bar(self):
        bar = ttk.Frame(self, padding=(6, 6))
        bar.pack(fill=tk.X)
        ttk.Label(bar, text="Port:").pack(side=tk.LEFT)
        self.port_var = tk.StringVar(value=self.settings.get("port", ""))
        self.port_cb = ttk.Combobox(bar, textvariable=self.port_var, width=34, state="readonly")
        self.port_cb.pack(side=tk.LEFT, padx=4)
        ttk.Button(bar, text="↺", width=3, command=self.refresh_ports).pack(side=tk.LEFT)
        self.btn_connect = ttk.Button(bar, text="Connect", command=self.toggle_connect)
        self.btn_connect.pack(side=tk.LEFT, padx=6)
        self.conn_var = tk.StringVar(value="Disconnected")
        self.conn_lbl = ttk.Label(bar, textvariable=self.conn_var, foreground="#b00000")
        self.conn_lbl.pack(side=tk.LEFT, padx=6)
        ttk.Label(bar, text="UID:").pack(side=tk.LEFT, padx=(16, 2))
        self.uid_var = tk.StringVar(value="—")
        ttk.Label(bar, textvariable=self.uid_var, width=10, relief="sunken").pack(side=tk.LEFT)
        ttk.Label(bar, text="Board clock:").pack(side=tk.LEFT, padx=(16, 2))
        self.clock_var = tk.StringVar(value="—")
        ttk.Label(bar, textvariable=self.clock_var, width=20, relief="sunken").pack(side=tk.LEFT)
        ttk.Button(bar, text="A+", width=3,
                   command=lambda: self.set_font_size(self.font_size + 1)).pack(side=tk.RIGHT)
        ttk.Button(bar, text="A−", width=3,
                   command=lambda: self.set_font_size(self.font_size - 1)).pack(side=tk.RIGHT)
        ttk.Label(bar, text="Text size:").pack(side=tk.RIGHT, padx=(16, 2))
        self.refresh_ports()

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        labels = [f"{p.device} — {p.description}" for p in ports]
        self.port_cb["values"] = labels
        cur = self.port_var.get().split(" ")[0]
        match = [lab for lab in labels if lab.split(" ")[0] == cur]
        if match:
            self.port_var.set(match[0])
        elif labels:
            self.port_var.set(labels[0])

    def toggle_connect(self):
        if self.board.is_open or getattr(self, "_reconnect_until", 0):
            self._stop_reconnect()
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_var.get().split(" ")[0]
        if not port:
            messagebox.showwarning("No port", "Select a serial port first.")
            return
        self._stop_reconnect()
        try:
            self._open(port)
        except serial.SerialException as e:
            messagebox.showerror("Connection error", str(e))

    def _open(self, port: str):
        """Open `port` and update the bar; raises serial.SerialException."""
        self.board.connect(port)
        info = next((p for p in serial.tools.list_ports.comports() if p.device == port), None)
        if info is not None and info.serial_number:
            self.settings["port_serial"] = info.serial_number   # identifies the board on reconnect
        self.settings["port"] = port
        self.refresh_ports()
        self.conn_var.set(f"Connected {port}")
        self.conn_lbl.configure(foreground="#007000")
        self.btn_connect.configure(text="Disconnect")
        self.device.log(f"[connected {port}]\n")
        self.after(300, lambda: self.board.is_open and self.send("?u", log=False))

    # ── automatic reconnect (after an upload or a reboot) ─────────────────────

    RECONNECT_S = 10.0

    def reconnect_later(self, reason: str, delay: float = 0.5, on_connected=None):
        """Try to reconnect to the board for RECONNECT_S seconds, starting after `delay`.
        The board is found by its USB serial number, so a changed COM number does not matter;
        a link that drops again while the board is still restarting is retried.
        `on_connected` runs once, at the first successful connection."""
        self._stop_reconnect()
        self._on_reconnected = on_connected
        port = self.settings.get("port") or self.port_var.get().split(" ")[0]
        if not port:
            return
        self._reconnect_until = time.time() + delay + self.RECONNECT_S
        self.device.log(f"[{reason}: reconnecting for up to {self.RECONNECT_S:.0f} s]\n")
        self.conn_var.set("Reconnecting …")
        self.conn_lbl.configure(foreground="#b07000")
        self._reconnect_id = self.after(int(delay * 1000), self._reconnect_attempt)

    def _find_board_port(self):
        sn = self.settings.get("port_serial")
        ports = serial.tools.list_ports.comports()
        if sn:
            match = [p.device for p in ports if p.serial_number == sn]
            if match:
                return match[0]
        port = self.settings.get("port")
        return port if any(p.device == port for p in ports) else None

    def _reconnect_attempt(self):
        self._reconnect_id = None
        if self.board.is_open:
            return
        port = self._find_board_port()
        if port:
            try:
                self._open(port)
            except serial.SerialException:
                self.board.disconnect()
            else:
                callback, self._on_reconnected = getattr(self, "_on_reconnected", None), None
                if callback:
                    callback()
                return
        if time.time() < self._reconnect_until:
            self._reconnect_id = self.after(300, self._reconnect_attempt)
        else:
            self._reconnect_until = 0
            self.conn_var.set("Disconnected")
            self.conn_lbl.configure(foreground="#b00000")
            self.device.log("[board not found again — connect manually]\n")

    def _stop_reconnect(self):
        if getattr(self, "_reconnect_id", None):
            self.after_cancel(self._reconnect_id)
        self._reconnect_id = None
        self._reconnect_until = 0
        self._on_reconnected = None

    def disconnect(self):
        was_open = self.board.is_open
        self.board.disconnect()
        self.conn_var.set("Disconnected")
        self.conn_lbl.configure(foreground="#b00000")
        self.btn_connect.configure(text="Connect")
        if was_open:
            self.device.log("[disconnected]\n")

    def send(self, text: str, log: bool = True) -> bool:
        if not self.board.is_open:
            messagebox.showwarning("Not connected", "Connect to the board first.")
            return False
        try:
            self.board.write(text)
        except serial.SerialException as e:
            self.device.log(f"[send failed: {e}]\n")
            return False
        if log:
            self.device.log(f"[tx] {text!r}\n")
        return True

    def send_sequence(self, commands, done=None, gap=0.15):
        """Send commands from a worker thread, `gap` seconds apart (the menu reads one
        command per loop pass); `done` runs on the GUI thread afterwards."""
        if not self.board.is_open:
            messagebox.showwarning("Not connected", "Connect to the board first.")
            return

        def work():
            ok = True
            for cmd in commands:
                try:
                    self.board.write(cmd)
                except serial.SerialException:
                    ok = False
                    break
                self.events.put(("log", f"[tx] {cmd!r}\n"))
                time.sleep(gap)
            time.sleep(0.4)                 # let the last replies arrive
            if done:
                self.events.put(("call", lambda: done(ok)))

        threading.Thread(target=work, daemon=True).start()

    # ── event pump ────────────────────────────────────────────────────────────

    def _poll(self):
        try:
            for _ in range(500):
                kind, payload = self.events.get_nowait()
                if kind == "rx":
                    self.device.log(payload)
                    self._parse_reply(payload)
                elif kind == "log":
                    self.device.log(payload)
                elif kind == "lost":
                    if payload is self.board.ser:     # current link died (board reset, cable)
                        self.disconnect()
                        if time.time() < getattr(self, "_reconnect_until", 0):
                            # still restarting: keep trying within the reconnect window
                            self._reconnect_id = self.after(300, self._reconnect_attempt)
                elif kind == "fw":
                    self.firmware.log(payload)
                elif kind == "fw_done":
                    self.firmware.finished(payload)
                elif kind == "call":
                    payload()
        except queue.Empty:
            pass
        self._poll_id = self.after(50, self._poll)

    def _parse_reply(self, line: str):
        """Fill fields from '?<key>' replies such as 'a = 60' or 'd = 2026-10-12 13:49:59'."""
        line = line.strip()
        if " = " not in line:
            return
        key, val = (p.strip() for p in line.split(" = ", 1))
        if key == "u":
            self.uid_var.set(val)
        elif key == "d":
            self.clock_var.set(val)
        elif key == "f":
            try:
                self.config_tab.set_value("f", str(int(val) // 1000))
            except ValueError:
                pass
        elif key in ("p",):
            return                              # PROC_MODE under the menu's own key; 'm' covers it
        else:
            cfg_key = SERIAL_TO_CFG.get(key, key)
            if cfg_key in CONFIG_KEYS:
                self.config_tab.set_value(cfg_key, val)

    def on_close(self):
        if self.proc and self.proc.poll() is None:
            if not messagebox.askyesno("Script running",
                                       "A firmware script is still running. Quit anyway?"):
                return
            self.proc.kill()
        self.settings["tab"] = self.nb.index(self.nb.select())
        self.settings["data_dir"] = str(self.data.current_dir)
        self.settings["target"] = self.firmware.target_var.get()
        save_settings(self.settings)
        self.disconnect()
        self._stop_reconnect()
        self.after_cancel(self._poll_id)
        self.destroy()


# ── Firmware tab ──────────────────────────────────────────────────────────────

class FirmwareTab(ttk.Frame):

    NOISE = ("System.Management.Automation.RemoteException", "NativeCommandError",
             "+ CategoryInfo", "+ FullyQualifiedErrorId", "At line:", "+ ~", "+ & $CLI")

    def __init__(self, parent, app: Workbench):
        super().__init__(parent, padding=8)
        self.app = app
        self.buttons = []
        self.busy = False
        have_source = CONFIG_H.is_file()
        self.cli, why = find_arduino()

        # ── mode switch ──────────────────────────────────────────────────────
        bar = self.mode_bar = ttk.Frame(self)
        bar.pack(fill=tk.X, pady=(0, 6))
        ttk.Label(bar, text="Mode:").pack(side=tk.LEFT)
        user = app.user_mode or not have_source or why is not None
        self.ui_mode = tk.StringVar(value="user" if user else "developer")
        self.dev_radio = ttk.Radiobutton(bar, text="Developer — build from source", value="developer",
                                         variable=self.ui_mode, command=self._show_mode)
        self.dev_radio.pack(side=tk.LEFT, padx=6)
        ttk.Radiobutton(bar, text="User — upload a firmware file (.uf2)", value="user",
                        variable=self.ui_mode, command=self._show_mode).pack(side=tk.LEFT, padx=6)
        if not have_source:
            self.dev_radio.configure(state=tk.DISABLED)

        # without Arduino, developer mode stays disabled until a re-check finds it
        self.arduino_bar = ttk.Frame(self)
        self.arduino_var = tk.StringVar()
        ttk.Label(self.arduino_bar, textvariable=self.arduino_var, foreground="#a05000",
                  wraplength=900, justify=tk.LEFT).pack(side=tk.LEFT)
        ttk.Button(self.arduino_bar, text="Check again",
                   command=self.check_arduino).pack(side=tk.LEFT, padx=8)
        if have_source:
            self._show_arduino(why)

        # ── developer controls ───────────────────────────────────────────────
        self.dev = ttk.Frame(self)
        box = ttk.LabelFrame(self.dev, text="Target", padding=6)
        box.pack(side=tk.LEFT, fill=tk.Y)
        self.target_var = tk.StringVar(value=app.settings.get("target", "rp2040"))
        for t, label in (("rp2040", "RP2040 Feather Adalogger"), ("rp2350", "RP2350 Feather HSTX")):
            ttk.Radiobutton(box, text=label, value=t, variable=self.target_var).pack(anchor="w")

        box = ttk.LabelFrame(self.dev, text="PROC_MODE (config.h)", padding=6)
        box.pack(side=tk.LEFT, fill=tk.Y, padx=8)
        self.mode_var = tk.IntVar(value=get_proc_mode() if have_source else 1)
        for m, label in ((0, "0  raw WAV (.wav)"), (1, "1  compressed (.bin)")):
            ttk.Radiobutton(box, text=label, value=m, variable=self.mode_var,
                            command=self._mode_changed).pack(anchor="w")

        box = ttk.LabelFrame(self.dev, text="Actions", padding=6)
        box.pack(side=tk.LEFT, fill=tk.Y)
        for text, cmd in (("Build", self.build), ("Upload", self.upload),
                          ("Build + Upload", self.flash), ("Clean", self.clean)):
            b = ttk.Button(box, text=text, command=cmd, width=16)
            b.pack(side=tk.LEFT, padx=3)
            self.buttons.append(b)
        self.stop_btn = ttk.Button(box, text="Stop", command=self.stop, state=tk.DISABLED)
        self.stop_btn.pack(side=tk.LEFT, padx=(12, 3))

        # ── user controls ────────────────────────────────────────────────────
        self.usr = ttk.LabelFrame(self, text="Upload a firmware file", padding=8)
        row = ttk.Frame(self.usr)
        row.pack(fill=tk.X)
        ttk.Label(row, text="File:").pack(side=tk.LEFT)
        self.uf2_var = tk.StringVar(value=app.settings.get("uf2_file", ""))
        ttk.Entry(row, textvariable=self.uf2_var, width=80).pack(side=tk.LEFT, fill=tk.X,
                                                                  expand=True, padx=4)
        ttk.Button(row, text="Browse…", command=self._browse_uf2).pack(side=tk.LEFT, padx=2)
        b = ttk.Button(row, text="Upload", command=self.upload_file, width=12)
        b.pack(side=tk.LEFT, padx=(8, 0))
        self.buttons.append(b)
        self.uf2_info_var = tk.StringVar()
        ttk.Label(self.usr, textvariable=self.uf2_info_var, foreground="#0050a0").pack(anchor="w", pady=(6, 0))
        ttk.Label(self.usr, foreground="#666666", justify=tk.LEFT, text=(
            "The board is reset into its boot loader through the serial port selected above, or, if it "
            "is already in BOOTSEL mode\n(hold BOOT, press RESET), the file is copied directly. "
            "A file built for the other chip (RP2040 / RP2350) is refused.")).pack(anchor="w", pady=(4, 0))
        self.uf2_var.trace_add("write", lambda *_: self._check_uf2())

        self.status_var = tk.StringVar(value="Idle")
        self.status_lbl = ttk.Label(self, textvariable=self.status_var)
        self.project_lbl = ttk.Label(self, text=f"Project: {ROOT}", foreground="#666666")

        self.text = scrolledtext.ScrolledText(self, font="TkFixedFont", state=tk.DISABLED)
        self.text.pack(fill=tk.BOTH, expand=True, pady=(6, 0))
        self.text.tag_configure("ok", foreground="#007000")
        self.text.tag_configure("err", foreground="#b00000")
        self.text.tag_configure("head", foreground="#0050a0")
        self._show_mode()
        self._check_uf2()

    def _show_mode(self):
        user = self.ui_mode.get() == "user"
        self.text.pack_forget()
        for w in (self.dev, self.usr, self.status_lbl, self.project_lbl):
            w.pack_forget()
        (self.usr if user else self.dev).pack(fill=tk.X)
        self.status_lbl.pack(anchor="w", pady=(8, 2))
        if not user:
            self.project_lbl.pack(anchor="w")
        self.text.pack(fill=tk.BOTH, expand=True, pady=(6, 0))

    def _show_arduino(self, why):
        """Enable developer mode, or disable it and show `why` with a re-check button."""
        if why is None:
            self.arduino_bar.pack_forget()
            self.dev_radio.configure(state=tk.NORMAL)
            return
        self.arduino_var.set(why)
        self.arduino_bar.pack(fill=tk.X, pady=(0, 6), after=self.mode_bar)
        self.dev_radio.configure(state=tk.DISABLED)
        if self.ui_mode.get() == "developer":
            self.ui_mode.set("user")
            self._show_mode()

    def check_arduino(self):
        self.cli, why = find_arduino()
        self._show_arduino(why)
        if why is None:
            self.status_var.set(f"Arduino found: {self.cli} — developer mode available")

    def _browse_uf2(self):
        cur = Path(self.uf2_var.get()) if self.uf2_var.get() else None
        # the user package keeps its ready-built files in firmware/
        default = ROOT / "firmware" if (ROOT / "firmware").is_dir() else ROOT / "build"
        start = cur.parent if cur and cur.parent.is_dir() else default
        p = filedialog.askopenfilename(parent=self, title="Firmware file", initialdir=start,
                                       filetypes=[("UF2 firmware", "*.uf2"), ("all files", "*.*")])
        if p:
            self.uf2_var.set(str(Path(p)))

    def _check_uf2(self):
        p = self.uf2_var.get().strip()
        if not p:
            self.uf2_info_var.set("No file selected.")
            return None
        try:
            info = uf2_info(p)
        except (OSError, UploadError) as e:
            self.uf2_info_var.set(f"✗ {e}")
            return None
        st = Path(p).stat()
        self.uf2_info_var.set(f"✓ UF2 for {info['chip']}, {info['bytes'] / 1024:.0f} kB, "
                              f"modified {datetime.fromtimestamp(st.st_mtime):%Y-%m-%d %H:%M}")
        return info

    def upload_file(self):
        if self.busy or (self.app.proc and self.app.proc.poll() is None):
            return
        path = self.uf2_var.get().strip()
        if self._check_uf2() is None:
            messagebox.showerror("Firmware file", self.uf2_info_var.get().lstrip("✗ "), parent=self)
            return
        self.app.settings["uf2_file"] = path
        port = self.app.port_var.get().split(" ")[0] or None
        self.app._stop_reconnect()
        self._uploading = True
        if self.app.board.is_open:
            self.app.disconnect()
            self.log("[serial port released for upload]\n", "head")
        self.log(f"\n> upload {path}" + (f"  (port {port})" if port else "") + "\n", "head")
        self._set_busy(True, "Uploading …")

        def work():
            try:
                upload_uf2(path, port, log=lambda m: self.app.events.put(("fw", m + "\n")))
                rc = 0
            except UploadError as e:
                self.app.events.put(("fw", f"ERROR: {e}\n"))
                rc = 1
            self.app.events.put(("fw_done", rc))

        threading.Thread(target=work, daemon=True).start()

    def _set_busy(self, busy: bool, status: str = "", can_stop: bool = False):
        self.busy = busy
        for b in self.buttons:
            b.configure(state=tk.DISABLED if busy else tk.NORMAL)
        self.stop_btn.configure(state=tk.NORMAL if busy and can_stop else tk.DISABLED)
        if status:
            self.status_var.set(status)

    def _mode_changed(self):
        mode = self.mode_var.get()
        try:
            if get_proc_mode() != mode:
                set_proc_mode(mode)
                self.log(f"config.h: PROC_MODE set to {mode} — rebuild to apply\n", "head")
        except (OSError, RuntimeError) as e:
            messagebox.showerror("config.h", str(e))
            self.mode_var.set(get_proc_mode())

    def log(self, line: str, tag: str = None):
        if any(n in line for n in self.NOISE):
            return
        if tag is None:
            low = line.lower()
            if line.startswith("OK ") or "upload ok" in low or "all builds passed" in low:
                tag = "ok"
            elif "error" in low or "failed" in low or "not detected" in low:
                tag = "err"
            elif line.startswith("===") or line.startswith("\n==="):
                tag = "head"
        self.text.configure(state=tk.NORMAL)
        self.text.insert(tk.END, line if line.endswith("\n") else line + "\n", tag)
        self.text.see(tk.END)
        self.text.configure(state=tk.DISABLED)

    def _run(self, script: str, *args, uses_port: bool = False):
        if self.busy:
            return
        # keep the PROC_MODE choice and config.h in step before compiling
        self.mode_var.set(get_proc_mode())
        self._uploading = uses_port
        if uses_port:
            self.app._stop_reconnect()
        if uses_port and self.app.board.is_open:
            self.app.disconnect()
            self.log("[serial port released for upload]\n", "head")
        cmd = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
               "-File", str(SCRIPTS / script), *args]
        self.log(f"\n> {script} {' '.join(args)}   (PROC_MODE {get_proc_mode()})\n", "head")
        env = dict(os.environ)
        if self.cli:
            env["ARDUINO_CLI"] = str(self.cli)      # the scripts use the one found here
        try:
            self.app.proc = subprocess.Popen(
                cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL, text=True, encoding="utf-8", errors="replace",
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except OSError as e:
            self.log(f"cannot start PowerShell: {e}\n", "err")
            return
        self._set_busy(True, f"Running {script} …", can_stop=True)
        proc = self.app.proc

        def pump():
            for line in proc.stdout:
                self.app.events.put(("fw", line))
            self.app.events.put(("fw_done", proc.wait()))

        threading.Thread(target=pump, daemon=True).start()

    def finished(self, rc: int):
        self.status_var.set("Done" if rc == 0 else f"Failed (exit code {rc})")
        self.log(f"[exit code {rc}]\n", "ok" if rc == 0 else "err")
        self._set_busy(False)
        if rc == 0 and getattr(self, "_uploading", False):
            # show the board's boot output as soon as it is connected again
            self.app.reconnect_later("upload done", delay=1.0,
                                     on_connected=lambda: self.app.nb.select(self.app.device))
        self._uploading = False

    def stop(self):
        if self.app.proc and self.app.proc.poll() is None:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(self.app.proc.pid)],
                           capture_output=True)

    def build(self):
        self._run("build.ps1", self.target_var.get())

    def upload(self):
        self._run(f"upload_{self.target_var.get()}.ps1", uses_port=True)

    def flash(self):
        self._run(f"flash_{self.target_var.get()}.ps1", uses_port=True)

    def clean(self):
        self._run("clean.ps1", self.target_var.get())


# ── Configuration tab ─────────────────────────────────────────────────────────

class ConfigTab(ttk.Frame):

    UNITS = {"f": "kHz", "a": "s", "o": "min", "r": "min", "g": "dB",
             "1": "h", "2": "h", "3": "h", "4": "h"}

    def __init__(self, parent, app: Workbench):
        super().__init__(parent, padding=8)
        self.app = app
        self.path = None

        form = ttk.LabelFrame(self, text=f"{CONFIG_FILENAME} parameters", padding=8)
        form.pack(side=tk.LEFT, fill=tk.Y)
        self.vars = {}
        for i, (key, _sep, comment) in enumerate(CONFIG_LINES):
            ttk.Label(form, text=f"!{key}", font="WbFixedBold").grid(
                row=i, column=0, sticky="e", padx=(0, 6), pady=2)
            v = tk.StringVar()
            self.vars[key] = v
            if key == "m":
                w = ttk.Combobox(form, textvariable=v, values=("0", "1"), width=25, state="readonly")
            else:
                w = ttk.Entry(form, textvariable=v, width=28)
            w.grid(row=i, column=1, pady=2)
            unit = self.UNITS.get(key, "")
            ttk.Label(form, text=unit, width=4).grid(row=i, column=2, sticky="w")
            ttk.Label(form, text=comment, foreground="#555555").grid(row=i, column=3, sticky="w")
        ttk.Label(form, foreground="#777777", justify=tk.LEFT, text=(
            "Strings: one word, max 39 characters.  !x: yyyy-mm-dd_hh:mm:ss\n"
            "!m is compile-time: it is written to the file but set on the Firmware tab.\n"
            "Recording hours: record in [!1, !2) and [!3, !4); 0 12 12 24 = always.")
        ).grid(row=len(CONFIG_LINES), column=0, columnspan=4, sticky="w", pady=(10, 0))

        side = ttk.Frame(self, padding=(12, 0))
        side.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        box = ttk.LabelFrame(side, text="Local file", padding=8)
        box.pack(fill=tk.X)
        for text, cmd in (("Defaults from config.h", self.defaults), ("Load…", self.load),
                          ("Save", self.save), ("Save as…", self.save_as)):
            ttk.Button(box, text=text, command=cmd).pack(fill=tk.X, pady=2)
        self.path_var = tk.StringVar(value="(no file)")
        ttk.Label(box, textvariable=self.path_var, foreground="#0050a0",
                  wraplength=300).pack(anchor="w", pady=(6, 0))
        ttk.Label(box, foreground="#777777", wraplength=300, justify=tk.LEFT, text=(
            "To configure a card in a PC card reader, save as config.txt in the card's "
            "root folder; the recorder reads it at boot.")).pack(anchor="w", pady=(4, 0))

        box = ttk.LabelFrame(side, text="Board (serial)", padding=8)
        box.pack(fill=tk.X, pady=(10, 0))
        ttk.Button(box, text="Read from board", command=self.read_board).pack(fill=tk.X, pady=2)
        ttk.Button(box, text="Write to board", command=self.write_board).pack(fill=tk.X, pady=2)
        self.store_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(box, text="store config.txt on the board's SD card (w)",
                        variable=self.store_var).pack(anchor="w", pady=(4, 0))
        ttk.Label(box, foreground="#777777", wraplength=300, justify=tk.LEFT, text=(
            "Parameters take effect at once, except !x (start time, used at boot). "
            "!m cannot be changed on the board.")).pack(anchor="w", pady=(4, 0))

        self.status_var = tk.StringVar()
        ttk.Label(side, textvariable=self.status_var, wraplength=320,
                  justify=tk.LEFT).pack(anchor="w", pady=(12, 0))

        last = app.settings.get("config_file")
        if last and Path(last).is_file():
            self._load_path(Path(last))
        else:
            self.defaults()

    # values ------------------------------------------------------------------

    def set_value(self, key: str, val: str):
        self.vars[key].set(val)

    def values(self):
        """Validated file-domain values, or None after showing the errors."""
        values, errors = {}, []
        for key in CONFIG_KEYS:
            val = self.vars[key].get().strip()
            err = validate_config_value(key, val)
            if err is None and key == "x" and not TIME_RE.match(val):
                err = f"!x: {val!r} is not yyyy-mm-dd_hh:mm:ss"
            if err is None and key == "m" and val not in ("0", "1"):
                err = "!m: must be 0 or 1"
            if err:
                errors.append(err)
            else:
                values[key] = val
        if errors:
            messagebox.showerror("Invalid parameters", "Fix these fields first:\n\n  "
                                 + "\n  ".join(errors), parent=self)
            return None
        return values

    # local file --------------------------------------------------------------

    def defaults(self):
        try:
            d = config_h_defaults()
        except OSError as e:
            messagebox.showerror("config.h", str(e))
            return
        for key in CONFIG_KEYS:
            self.vars[key].set("" if d.get(key) is None else str(d[key]))
        self.status_var.set("Filled with the compile-time defaults of config.h.")

    def load(self):
        start = self.path.parent if self.path else ROOT / "data"
        p = filedialog.askopenfilename(parent=self, title="Load config.txt", initialdir=start,
                                       initialfile=CONFIG_FILENAME,
                                       filetypes=[("config file", "*.txt"), ("all files", "*.*")])
        if p:
            self._load_path(Path(p))

    def _load_path(self, path: Path):
        try:
            found = read_config_file(path)
        except OSError as e:
            messagebox.showerror("Load failed", str(e), parent=self)
            return
        missing = []
        for key in CONFIG_KEYS:
            if key in found:
                self.vars[key].set(found[key])
            elif key in CONFIG_DEFAULTS:
                self.vars[key].set(CONFIG_DEFAULTS[key])
            else:
                missing.append(key)
        self.path = path
        self.path_var.set(str(path))
        self.app.settings["config_file"] = str(path)
        self.status_var.set(f"Loaded {path.name}: {len(found)} keys"
                            + (f"; missing {' '.join(missing)} (fields unchanged)" if missing else ""))

    def save(self):
        if self.path is None:
            self.save_as()
        else:
            self._save_path(self.path)

    def save_as(self):
        start = self.path.parent if self.path else ROOT / "data"
        p = filedialog.asksaveasfilename(parent=self, title="Save config.txt", initialdir=start,
                                         initialfile=CONFIG_FILENAME, defaultextension=".txt",
                                         filetypes=[("config file", "*.txt"), ("all files", "*.*")])
        if p:
            self._save_path(Path(p))

    def _save_path(self, path: Path):
        values = self.values()
        if values is None:
            return
        try:
            write_config_file(path, values)
        except (OSError, UnicodeEncodeError) as e:
            messagebox.showerror("Save failed", str(e), parent=self)
            return
        self.path = path
        self.path_var.set(str(path))
        self.app.settings["config_file"] = str(path)
        self.status_var.set(f"Saved {path} at {datetime.now():%H:%M:%S}.")

    # board -------------------------------------------------------------------

    def read_board(self):
        keys = [CFG_TO_SERIAL.get(k, k) for k in CONFIG_KEYS]
        self.status_var.set("Reading parameters from the board …")
        self.app.send_sequence([f"?{k}" for k in keys] + ["?u"],
                               done=lambda ok: self.status_var.set(
                                   "Read from board." if ok else "Reading interrupted."))

    def write_board(self):
        values = self.values()
        if values is None:
            return
        cmds = []
        for key in CONFIG_KEYS:
            if key in CFG_ONLY:
                continue
            val = values[key]
            if key == "f":
                val = str(int(val) * 1000)
            cmds.append(f"!{CFG_TO_SERIAL.get(key, key)}{val}\n")
        if self.store_var.get():
            cmds.append("w")
        self.status_var.set("Writing parameters to the board …")

        def done(ok):
            if not ok:
                self.status_var.set("Writing interrupted — connection lost.")
                return
            self.status_var.set("Written to board" + (" and stored in config.txt."
                                                      if self.store_var.get() else "."))
            self.app.send_sequence(["?m"])     # board reports its compiled PROC_MODE

        self.app.send_sequence(cmds, done=done)


# ── Device tab ────────────────────────────────────────────────────────────────

class DeviceTab(ttk.Frame):

    MAX_LINES = 5000

    def __init__(self, parent, app: Workbench):
        super().__init__(parent, padding=8)
        self.app = app

        ctrl = ttk.LabelFrame(self, text="Control", padding=6)
        ctrl.pack(fill=tk.X)
        for text, cmd in (("▶ Start (s)", "s"), ("■ Stop (e)", "e"), ("Print (p)", "p"),
                          ("Store config (w)", "w")):
            ttk.Button(ctrl, text=text, command=lambda c=cmd: app.send(c)).pack(side=tk.LEFT, padx=3)
        ttk.Button(ctrl, text="Reboot", command=self.reboot).pack(side=tk.LEFT, padx=3)
        ttk.Separator(ctrl, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=8)
        ttk.Label(ctrl, text="Hibernate").pack(side=tk.LEFT)
        self.hib_var = tk.StringVar(value="1")
        ttk.Entry(ctrl, textvariable=self.hib_var, width=4).pack(side=tk.LEFT, padx=3)
        ttk.Button(ctrl, text="hours", command=lambda: self.hibernate("x")).pack(side=tk.LEFT)
        ttk.Button(ctrl, text="minutes", command=lambda: self.hibernate("y")).pack(side=tk.LEFT, padx=3)

        clk = ttk.LabelFrame(self, text="Clock", padding=6)
        clk.pack(fill=tk.X, pady=(8, 0))
        ttk.Button(clk, text="Read board clock", command=lambda: app.send("?d")).pack(side=tk.LEFT, padx=3)
        self.utc_var = tk.BooleanVar(value=app.settings.get("utc", False))
        ttk.Button(clk, text="Set board clock to PC time", command=self.sync_clock).pack(side=tk.LEFT, padx=3)
        ttk.Checkbutton(clk, text="UTC", variable=self.utc_var).pack(side=tk.LEFT, padx=6)
        ttk.Label(clk, text="Set to:").pack(side=tk.LEFT, padx=(16, 3))
        self.dt_var = tk.StringVar(value="yyyy-mm-dd hh:mm:ss")
        ttk.Entry(clk, textvariable=self.dt_var, width=20).pack(side=tk.LEFT)
        ttk.Button(clk, text="Set", command=self.set_clock).pack(side=tk.LEFT, padx=3)

        mon = ttk.LabelFrame(self, text="Serial monitor", padding=6)
        mon.pack(fill=tk.BOTH, expand=True, pady=(8, 0))
        self.text = scrolledtext.ScrolledText(mon, font="TkFixedFont", state=tk.DISABLED)
        self.text.pack(fill=tk.BOTH, expand=True)
        bar = ttk.Frame(mon)
        bar.pack(fill=tk.X, pady=(4, 0))
        self.raw_var = tk.StringVar()
        e = ttk.Entry(bar, textvariable=self.raw_var)
        e.pack(side=tk.LEFT, fill=tk.X, expand=True)
        e.bind("<Return>", lambda _: self.send_raw())
        ttk.Button(bar, text="Send", command=self.send_raw).pack(side=tk.LEFT, padx=4)
        ttk.Button(bar, text="Clear", command=self.clear).pack(side=tk.LEFT)

    def log(self, text: str):
        self.text.configure(state=tk.NORMAL)
        self.text.insert(tk.END, text)
        n = int(self.text.index("end-1c").split(".")[0])
        if n > self.MAX_LINES:
            self.text.delete("1.0", f"{n - self.MAX_LINES}.0")
        self.text.see(tk.END)
        self.text.configure(state=tk.DISABLED)

    def clear(self):
        self.text.configure(state=tk.NORMAL)
        self.text.delete("1.0", tk.END)
        self.text.configure(state=tk.DISABLED)

    # menu commands that restart the board: x0 / y0 (reboot), b (store config, reboot)
    REBOOT_RE = re.compile(r"^\s*(?:[xy]\s*0+|b)\s*$")

    def send_raw(self):
        text = self.raw_var.get()
        if text and self.app.send(text + "\n"):
            self.raw_var.set("")
            if self.REBOOT_RE.match(text):
                self._after_reboot()

    def reboot(self):
        if self.app.send("x0\n"):              # reboot without rewriting config.txt
            self._after_reboot()

    def _after_reboot(self):
        # let the board read the command, then drop the link and wait for it to come back
        def go():
            self.app.disconnect()
            self.app.reconnect_later("reboot", delay=0.5)
        self.app.after(300, go)

    def hibernate(self, cmd: str):
        val = self.hib_var.get().strip()
        if not val.isdigit() or int(val) == 0:
            messagebox.showerror("Hibernate", "Enter a positive whole number.")
            return
        self.app.send(f"{cmd}{val}\n")

    def _set_time(self, stamp: str):
        self.app.send_sequence([f"!d{stamp}\n", "?d"])

    def sync_clock(self):
        self.app.settings["utc"] = self.utc_var.get()
        now = datetime.now(timezone.utc) if self.utc_var.get() else datetime.now()
        # send at the start of the next second, so the board is not up to 1 s behind
        time.sleep(1 - now.microsecond / 1e6)
        now = datetime.now(timezone.utc) if self.utc_var.get() else datetime.now()
        self._set_time(now.strftime("%Y-%m-%d %H:%M:%S"))

    def set_clock(self):
        val = self.dt_var.get().strip()
        try:
            datetime.strptime(val, "%Y-%m-%d %H:%M:%S")
        except ValueError:
            messagebox.showerror("Clock", "Use yyyy-mm-dd hh:mm:ss")
            return
        self._set_time(val)


# ── Data tab ──────────────────────────────────────────────────────────────────

class DataTab(ttk.Frame):
    """Local-disk browser: folders and microPAM recordings; header popup and data viewer
    from micropam_browser.py."""

    COLUMNS = ("File", "Size", "Modified", "Mode", "fsamp", "nch", "Dur / Size")

    def __init__(self, parent, app: Workbench):
        super().__init__(parent, padding=8)
        self.app = app
        start = Path(app.settings.get("data_dir", str(Path.home())))
        self.current_dir = start if start.is_dir() else Path.home()
        self._popup = None
        self._viewer = None
        self._spectrum = None

        bar = ttk.Frame(self)
        bar.pack(fill=tk.X)
        ttk.Button(bar, text="⬆ Up", command=self.go_up).pack(side=tk.LEFT, padx=2)
        ttk.Button(bar, text="⟳ Refresh", command=self.refresh).pack(side=tk.LEFT, padx=2)
        ttk.Button(bar, text="📂 Open folder…", command=self.browse).pack(side=tk.LEFT, padx=2)
        ttk.Button(bar, text="Header", command=self.show_header).pack(side=tk.LEFT, padx=(12, 2))
        ttk.Button(bar, text="View data", command=self.show_data).pack(side=tk.LEFT, padx=2)
        ttk.Button(bar, text="Spectrum", command=self.show_spectrum).pack(side=tk.LEFT, padx=2)
        self.wav_btn = ttk.Button(bar, text="To WAV…", command=self.convert_wav)
        self.wav_btn.pack(side=tk.LEFT, padx=2)
        self.dir_var = tk.StringVar()
        ttk.Entry(bar, textvariable=self.dir_var, state="readonly").pack(
            side=tk.LEFT, fill=tk.X, expand=True, padx=(8, 2))

        frame = ttk.Frame(self)
        frame.pack(fill=tk.BOTH, expand=True, pady=(6, 0))
        vsb = ttk.Scrollbar(frame, orient=tk.VERTICAL)
        self.tree = ttk.Treeview(frame, columns=self.COLUMNS, show="headings",
                                 yscrollcommand=vsb.set, selectmode="extended")
        vsb.configure(command=self.tree.yview)
        widths = (280, 80, 145, 150, 90, 40, 90)
        for col, w in zip(self.COLUMNS, widths):
            self.tree.heading(col, text=col)
            self.tree.column(col, width=w, anchor=tk.W if col == "File" else tk.CENTER,
                             stretch=(col == "File"))
        self.tree.tag_configure("dir", foreground="#0050a0")
        self.tree.tag_configure("err", foreground="#999999")
        vsb.pack(side=tk.RIGHT, fill=tk.Y)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.status_var = tk.StringVar()
        ttk.Label(self, textvariable=self.status_var).pack(anchor="w", pady=(4, 0))
        ttk.Label(self, foreground="#777777", text=(
            "Double-click a folder to open it, a file for its header.  "
            "v or 'View data' plots a file, s or 'Spectrum' its Welch spectrum.  "
            "'To WAV…' converts the selected .bin files and folders (Ctrl/Shift-click).  "
            "Right-click for more.")).pack(anchor="w")

        self.tree.bind("<Double-Button-1>", self._on_double)
        self.tree.bind("<Return>", self._on_double)
        self.tree.bind("v", lambda _: self.show_data())
        self.tree.bind("s", lambda _: self.show_spectrum())
        self.tree.bind("<BackSpace>", lambda _: self.go_up())
        self.tree.bind("<Button-3>", self._on_right)
        self.menu = tk.Menu(self, tearoff=0)
        self.menu.add_command(label="Show header info", command=self.show_header)
        self.menu.add_command(label="View data", command=self.show_data)
        self.menu.add_command(label="Spectrum (Welch)", command=self.show_spectrum)
        self.menu.add_command(label="Convert to WAV…", command=self.convert_wav)
        self.menu.add_separator()
        self.menu.add_command(label="Open in Finder" if sys.platform == "darwin" else "Open in Explorer",
                              command=self.open_explorer)

        self.load(self.current_dir)

    def load(self, directory: Path):
        try:
            with busy(self.app):
                self._list(directory)
        except OSError as e:
            messagebox.showerror("Folder", str(e))

    def _list(self, directory: Path):
        entries = list(directory.iterdir())
        self.current_dir = directory
        self.dir_var.set(str(directory))
        self.tree.delete(*self.tree.get_children())
        dirs = sorted((p for p in entries if p.is_dir() and not p.name.startswith(".")),
                      key=lambda p: p.name.lower())
        files = sorted((p for p in entries if p.is_file() and p.suffix.lower() in EXTENSIONS),
                       key=lambda p: p.name)
        for d in dirs:
            self.tree.insert("", tk.END, iid=str(d), values=(f"📁 {d.name}", "", "", "", "", "", ""),
                             tags=("dir",))
        for p in files:
            st = p.stat()
            mtime = datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d %H:%M:%S")
            info = _safe_parse(p)
            if info is None or not info.get("riff_ok"):
                self.tree.insert("", tk.END, iid=str(p), tags=("err",),
                                 values=(p.name, _fmt_size(st.st_size), mtime,
                                         "empty file" if st.st_size == 0 else "not a microPAM file",
                                         "", "", ""))
                continue
            self.tree.insert("", tk.END, iid=str(p), values=(
                p.name, _fmt_size(st.st_size), mtime, mode_text(info),
                f"{info.get('sample_rate', '?')} Hz", str(info.get("n_channels", "?")),
                _fmt_duration(info, p)))
        cfg = directory / CONFIG_FILENAME
        self.status_var.set(f"{len(dirs)} folders, {len(files)} recordings"
                            + (f"  ·  {CONFIG_FILENAME} here (load it on the Configuration tab)"
                               if cfg.is_file() else ""))

    def refresh(self):
        self.load(self.current_dir)

    def go_up(self):
        if self.current_dir.parent != self.current_dir:
            self.load(self.current_dir.parent)

    def browse(self):
        d = filedialog.askdirectory(parent=self, initialdir=str(self.current_dir), title="Select folder")
        if d:
            self.load(Path(d))

    def _selected(self):
        sel = self.tree.selection()
        return Path(sel[0]) if sel else None

    def _on_double(self, _event=None):
        p = self._selected()
        if p is None:
            return
        if p.is_dir():
            self.load(p)
        else:
            self.show_header()

    def _on_right(self, event):
        row = self.tree.identify_row(event.y)
        if row:
            if row not in self.tree.selection():     # keep a multiple selection
                self.tree.selection_set(row)
            self.menu.tk_popup(event.x_root, event.y_root)

    def _file_info(self):
        p = self._selected()
        if p is None or p.is_dir():
            return None, None
        with busy(self.app):
            info = _safe_parse(p)
        if info is None or not info.get("riff_ok"):
            messagebox.showerror("Parse error", f"No microPAM header in\n{p}")
            return None, None
        return p, info

    def show_header(self):
        p, info = self._file_info()
        if p is None:
            return
        if self._popup and self._popup.winfo_exists():
            self._popup.destroy()
        with busy(self.app):
            self._popup = HeaderPopup(self.app, p, info)

    def show_data(self):
        p, info = self._file_info()
        if p is None:
            return
        if self._viewer and self._viewer.winfo_exists():
            self._viewer.destroy()
        self._viewer = DataViewer(self.app, p, info)

    def show_spectrum(self):
        p, info = self._file_info()
        if p is None:
            return
        if self._spectrum and self._spectrum.winfo_exists():
            self._spectrum.destroy()
        self._spectrum = SpectrumViewer(self.app, p, info)

    def convert_wav(self):
        """Convert the selected .bin files and folders to WAV (bin2wav.py) in a folder the
        user picks, in a worker thread; progress goes to the status line."""
        if self.wav_btn.instate(["disabled"]):
            return
        sources = [Path(i) for i in self.tree.selection()]
        sources = [p for p in sources if p.is_dir() or p.suffix.lower() == ".bin"]
        if not sources:
            messagebox.showinfo("Convert to WAV", "Select .bin files or folders first.", parent=self)
            return
        d = filedialog.askdirectory(parent=self, title="Folder for the WAV files",
                                    initialdir=self.app.settings.get("wav_dir", str(self.current_dir)))
        if not d:
            return
        dest = Path(d)
        self.app.settings["wav_dir"] = str(dest)
        with busy(self.app):
            pairs = bin2wav.plan(sources, dest)
        if not pairs:
            messagebox.showinfo("Convert to WAV", "No .bin files in the selection.", parent=self)
            return
        existing = sum(dst.exists() for _, dst in pairs)
        if existing:
            ans = messagebox.askyesnocancel(
                "Convert to WAV", f"{existing} of {len(pairs)} WAV files exist already in\n{dest}\n\n"
                "Overwrite them? (No skips them.)", parent=self)
            if ans is None:
                return
            if not ans:
                pairs = [(s, t) for s, t in pairs if not t.exists()]
                if not pairs:
                    self.status_var.set(f"All WAV files exist already in {dest}")
                    return
        self.wav_btn.state(["disabled"])
        post = lambda fn: self.app.events.put(("call", fn))

        def work():
            failed, filled = [], 0
            for k, (src, dst) in enumerate(pairs, 1):
                post(lambda k=k, src=src: self.status_var.set(
                    f"Converting {k}/{len(pairs)}: {src.name} …"))
                try:
                    filled += bin2wav.convert_file(src, dst)[1]
                except Exception as e:
                    failed.append(f"{src.name}: {e}")
            post(lambda: self._converted(dest, len(pairs), failed, filled))

        threading.Thread(target=work, daemon=True).start()

    def _converted(self, dest: Path, n: int, failed: list, filled: int):
        self.wav_btn.state(["!disabled"])
        self.status_var.set(f"{n - len(failed)} of {n} files converted to WAV in {dest}, "
                            f"{filled} missed records zero-filled")
        if failed:
            messagebox.showwarning("Convert to WAV", "Not converted:\n" + "\n".join(failed[:20])
                                   + (f"\n… and {len(failed) - 20} more" if len(failed) > 20 else ""),
                                   parent=self)
        if dest.resolve() == self.current_dir.resolve() or self.current_dir.resolve() in dest.resolve().parents:
            self.refresh()

    def open_explorer(self):
        p = self._selected() or self.current_dir
        folder = p if p.is_dir() else p.parent
        if sys.platform == "win32":
            os.startfile(folder)
        else:                           # as micropam_browser._open_folder
            subprocess.Popen(["open" if sys.platform == "darwin" else "xdg-open", str(folder)])


if __name__ == "__main__":
    Workbench(user_mode="--dev" not in sys.argv[1:]).mainloop()
