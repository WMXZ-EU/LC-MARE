"""
microPAM_V4 Configuration GUI
Communicates with the device over a serial port using the menu protocol
defined in src/menu.cxx.

Protocol summary
----------------
  s           – start acquisition
  e           – stop acquisition
  p           – print all parameters
  ?<key>      – query single parameter
  !<key><val> – set parameter (value sent as a subsequent line)
  c           – set RTC time (interactive)
  x<n>        – hibernate n hours (0 = reboot)
  y<n>        – hibernate n minutes (0 = reboot)
  b           – reboot

Parameter keys
--------------
  a  t_acq      (seconds)
  o  t_on       (minutes)
  r  t_rep      (minutes)
  f  fsamp      (Hz)
  g  again      (dB analog gain)
  n  IART       artist / creator
  k  IPRD       product / activity
  l  INAM       location id
  d  datetime   YYYY-MM-DD HH:MM:SS
  1..4  h_rec   recording hour windows
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, font as tkfont
import serial
import serial.tools.list_ports
import threading
import time
import queue as Queue
from datetime import datetime


# ── helpers ──────────────────────────────────────────────────────────────────

def list_ports():
    return [p.device for p in serial.tools.list_ports.comports()]


# ── main application ──────────────────────────────────────────────────────────

class MicroPAMGui(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("microPAM_V4 Configurator")
        self.resizable(True, True)

        self.ser = None
        self.rx_queue = Queue.Queue()
        self.rx_thread = None

        # Resize the default font globally so all widgets (including tk.Entry)
        # inherit 12 pt. The serial monitor overrides this with its own explicit font.
        for name in ("TkDefaultFont", "TkTextFont", "TkFixedFont"):
            tkfont.nametofont(name).configure(size=12)
        style = ttk.Style(self)
        for widget in ("TLabel", "TButton", "TEntry", "TCombobox", "TLabelframe.Label"):
            style.configure(widget, font=("TkDefaultFont", 12))

        self._build_ui()
        self._refresh_ports()

    # ── UI construction ───────────────────────────────────────────────────────

    def _build_ui(self):
        pad = dict(padx=6, pady=4)

        # Bottom row (serial monitor) expands; top rows are fixed height.
        self.rowconfigure(3, weight=1)
        self.columnconfigure(0, weight=1)

        # ── connection bar (full width) ───────────────────────────────────────
        conn = ttk.LabelFrame(self, text="Connection")
        conn.grid(row=0, column=0, sticky="ew", **pad)

        ttk.Label(conn, text="Port:").grid(row=0, column=0, **pad)
        self.port_var = tk.StringVar()
        self.port_cb = ttk.Combobox(conn, textvariable=self.port_var, width=14, state="readonly")
        self.port_cb.grid(row=0, column=1, **pad)

        self.btn_connect = ttk.Button(conn, text="Connect", command=self._toggle_connect)
        self.btn_connect.grid(row=0, column=2, **pad)

        ttk.Button(conn, text="↺ Ports", command=self._refresh_ports).grid(row=0, column=3, **pad)

        self.status_var = tk.StringVar(value="Disconnected")
        ttk.Label(conn, textvariable=self.status_var, foreground="red").grid(row=0, column=4, **pad)

        ttk.Label(conn, text="UID:").grid(row=0, column=5, **pad)
        self.uid_var = tk.StringVar(value="—")
        ttk.Label(conn, textvariable=self.uid_var, width=12, relief="sunken", anchor="w").grid(row=0, column=6, **pad)

        # ── parameters ────────────────────────────────────────────────────────
        params = ttk.LabelFrame(self, text="Parameters")
        params.grid(row=1, column=0, sticky="w", **pad)

        self.vars = {}

        def row(label, key, col=0, r=0, width=10):
            ttk.Label(params, text=label).grid(row=r, column=col*3,   sticky="e", **pad)
            v = tk.StringVar()
            self.vars[key] = v
            ttk.Entry(params, textvariable=v, width=width).grid(row=r, column=col*3+1, **pad)
            ttk.Button(params, text="Set", width=4,
                       command=lambda k=key: self._set_param(k)
                       ).grid(row=r, column=col*3+2, **pad)

        row("t_acq (s):",  "a", col=0, r=0)
        row("t_on (min):", "o", col=0, r=1)
        row("t_rep (min):","r", col=0, r=2)
        row("fsamp (Hz):", "f", col=0, r=3)
        row("again (dB):", "g", col=0, r=4)

        row("Artist (n):",    "n", col=1, r=0, width=14)
        row("Product (k):",  "k", col=1, r=1, width=14)
        row("Name (l):",     "l", col=1, r=2, width=14)
        row("Start Time (x):","x", col=1, r=3, width=17)

        # h_rec hour windows
        ttk.Label(params, text="h_rec (hours):").grid(row=5, column=0, sticky="e", **pad)
        hrec_frame = ttk.Frame(params)
        hrec_frame.grid(row=5, column=1, columnspan=5, sticky="w", **pad)
        self.hrec_vars = []
        for i in range(4):
            v = tk.StringVar()
            self.hrec_vars.append(v)
            ttk.Label(hrec_frame, text=f"{i+1}:").pack(side="left")
            ttk.Entry(hrec_frame, textvariable=v, width=4).pack(side="left", padx=2)
        ttk.Button(hrec_frame, text="Set all", command=self._set_hrec).pack(side="left", padx=4)

        # datetime
        ttk.Label(params, text="Datetime:").grid(row=6, column=0, sticky="e", **pad)
        self.dt_var = tk.StringVar()
        ttk.Entry(params, textvariable=self.dt_var, width=20).grid(row=6, column=1, **pad)
        ttk.Button(params, text="getRTC",  command=self._get_rtc).grid(row=6, column=2, **pad)
        ttk.Button(params, text="Set RTC", command=self._set_datetime).grid(row=6, column=3, **pad)
        ttk.Button(params, text="syncRTC", command=self._sync_rtc).grid(row=6, column=4, **pad)

        # ── control buttons ───────────────────────────────────────────────────
        ctrl = ttk.LabelFrame(self, text="Control")
        ctrl.grid(row=2, column=0, sticky="ew", **pad)

        ttk.Button(ctrl, text="▶ Start",     command=lambda: self._send("s")).pack(side="left", **pad)
        ttk.Button(ctrl, text="■ Stop",       command=lambda: self._send("e")).pack(side="left", **pad)
        ttk.Button(ctrl, text="Print params", command=lambda: self._send("p")).pack(side="left", **pad)
        ttk.Button(ctrl, text="Read all",     command=self._read_all).pack(side="left", **pad)
        ttk.Button(ctrl, text="Reboot",       command=self._reboot).pack(side="left", **pad)

        ttk.Label(ctrl, text="  Hibernate:").pack(side="left")
        self.hib_val = tk.StringVar(value="1")
        ttk.Entry(ctrl, textvariable=self.hib_val, width=4).pack(side="left")
        ttk.Button(ctrl, text="hours",   command=lambda: self._hibernate("x")).pack(side="left", **pad)
        ttk.Button(ctrl, text="minutes", command=lambda: self._hibernate("y")).pack(side="left", **pad)

        # ── serial monitor (full width, bottom) ───────────────────────────────
        mon = ttk.LabelFrame(self, text="Serial monitor")
        mon.grid(row=3, column=0, sticky="nsew", **pad)
        mon.rowconfigure(0, weight=1)
        mon.columnconfigure(0, weight=1)

        self.monitor = scrolledtext.ScrolledText(mon, height=16, state="disabled",
                                                 font=("Courier", 9))
        self.monitor.grid(row=0, column=0, sticky="nsew", padx=4, pady=4)

        send_bar = ttk.Frame(mon)
        send_bar.grid(row=1, column=0, sticky="ew", padx=4, pady=(0, 4))
        self.raw_var = tk.StringVar()
        ttk.Entry(send_bar, textvariable=self.raw_var).pack(side="left", fill="x", expand=True)
        ttk.Button(send_bar, text="Send",  command=self._send_raw).pack(side="left", padx=4)
        ttk.Button(send_bar, text="Clear", command=self._clear_monitor).pack(side="left")

        self.after(100, self._poll_rx)

    # ── serial connection ─────────────────────────────────────────────────────

    def _refresh_ports(self):
        ports = list_ports()
        self.port_cb["values"] = ports
        if ports:
            self.port_var.set(ports[0])

    def _toggle_connect(self):
        if self.ser and self.ser.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self.port_var.get()
        try:
            self.ser = serial.Serial(port, 115200, timeout=0.1)
            self.status_var.set("Connected")
            self.btn_connect.config(text="Disconnect")
            self._log(f"[connected {port}]\n")
            self.rx_thread = threading.Thread(target=self._rx_worker, daemon=True)
            self.rx_thread.start()
            self.after(300, lambda: self._send("?u"))
        except Exception as e:
            messagebox.showerror("Connection error", str(e))

    def _disconnect(self):
        if self.ser:
            self.ser.close()
            self.ser = None
        self.status_var.set("Disconnected")
        self.uid_var.set("—")
        self.btn_connect.config(text="Connect")
        self._log("[disconnected]\n")

    def _rx_worker(self):
        while self.ser and self.ser.is_open:
            try:
                line = self.ser.readline()
                if line:
                    self.rx_queue.put(line.decode("utf-8", errors="replace"))
            except Exception:
                break

    def _poll_rx(self):
        while not self.rx_queue.empty():
            line = self.rx_queue.get()
            self._log(line)
            self._parse_response(line)
        self.after(100, self._poll_rx)

    # ── send helpers ──────────────────────────────────────────────────────────

    def _send(self, cmd: str):
        if not self._check_conn():
            return
        self.ser.write(cmd.encode())
        self._log(f"[tx] {cmd!r}\n")

    def _send_line(self, text: str):
        if not self._check_conn():
            return
        self.ser.write((text + "\n").encode())

    def _send_raw(self):
        self._send(self.raw_var.get())

    def _check_conn(self) -> bool:
        if self.ser and self.ser.is_open:
            return True
        messagebox.showwarning("Not connected", "Connect to a serial port first.")
        return False

    # ── parameter get/set ─────────────────────────────────────────────────────

    def _set_param(self, key: str):
        val = self.vars[key].get().strip()
        if not val:
            return
        self._send(f"!{key}")
        time.sleep(0.05)
        self._send_line(val)

    def _set_hrec(self):
        for i, v in enumerate(self.hrec_vars):
            val = v.get().strip()
            if val:
                key = str(i + 1)
                self._send(f"!{key}")
                time.sleep(0.05)
                self._send_line(val)
                time.sleep(0.05)

    def _set_datetime(self):
        dt_str = self.dt_var.get().strip()
        if not dt_str:
            return
        # validate format
        try:
            datetime.strptime(dt_str, "%Y-%m-%d %H:%M:%S")
        except ValueError:
            messagebox.showerror("Format error", "Use YYYY-MM-DD HH:MM:SS")
            return
        # firmware expects the same format with any separators; send via !d
        self._send("!d")
        time.sleep(0.05)
        self._send_line(dt_str)

    def _get_rtc(self):
        """Query the MCU RTC and populate the Datetime field from the response."""
        self._send("?d")

    def _sync_rtc(self):
        """Send PC time to the MCU RTC and update the Datetime field."""
        dt_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.dt_var.set(dt_str)
        self._send("!d")
        time.sleep(0.05)
        self._send_line(dt_str)

    def _read_all(self):
        """Query every readable parameter in sequence."""
        if not self._check_conn():
            return
        for key in ("a", "o", "r", "f", "g", "n", "k", "l", "x", "u", "p",
                    "1", "2", "3", "4"):
            self._send(f"?{key}")
            time.sleep(0.12)

    def _reboot(self):
        self._send("b")

    def _hibernate(self, mode: str):
        val = self.hib_val.get().strip()
        if not val:
            return
        self._send(mode)
        time.sleep(0.05)
        self._send_line(val)

    # ── response parser — fills GUI fields from ?<key> replies ───────────────

    KEY_MAP = {
        "a": "a", "o": "o", "r": "r", "f": "f", "g": "g",
        "n": "n", "k": "k", "l": "l", "x": "x",
    }

    def _parse_response(self, line: str):
        line = line.strip()
        # responses look like:  "a = 60"  or  "d = 2026-06-26 15:30:00"
        if " = " not in line:
            return
        parts = line.split(" = ", 1)
        if len(parts) != 2:
            return
        key, val = parts[0].strip(), parts[1].strip()
        if key == "d":
            # firmware prints date and time in two printf calls on the same line:
            # "d = 2026-06-26 15:30:00"
            self.dt_var.set(val)
        elif key == "u":
            self.uid_var.set(val)
        elif key in self.KEY_MAP:
            self.vars[self.KEY_MAP[key]].set(val)
        elif key in ("1", "2", "3", "4"):
            idx = int(key) - 1
            self.hrec_vars[idx].set(val)

    # ── monitor helpers ───────────────────────────────────────────────────────

    def _log(self, text: str):
        self.monitor.config(state="normal")
        self.monitor.insert("end", text)
        self.monitor.see("end")
        self.monitor.config(state="disabled")

    def _clear_monitor(self):
        self.monitor.config(state="normal")
        self.monitor.delete("1.0", "end")
        self.monitor.config(state="disabled")

    def on_close(self):
        self._disconnect()
        self.destroy()


if __name__ == "__main__":
    app = MicroPAMGui()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()
