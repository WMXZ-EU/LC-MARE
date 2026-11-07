"""
uf2_upload.py — upload a ready-built .uf2 firmware file (no compiler, no Arduino IDE)
====================================================================================

User-mode upload for the microPAM recorders on RP2040 / RP2350. Needs only Python and
pyserial.

Procedure
---------
1. The .uf2 file is checked: valid UF2 blocks, and the chip family it was built for
   (RP2040, or RP2350).
2. If a boot drive is already mounted (board held in BOOTSEL mode), the file is copied to it.
3. Otherwise the board is reset into its boot loader through the serial port (open the port
   at 1200 baud and drop DTR — the arduino-pico core reboots into BOOTSEL on that), and the
   boot drive is awaited.
4. The file is copied to the boot drive; the board then flashes it and restarts.

A boot drive is recognised by the INFO_UF2.TXT the boot ROM puts on it ("Board-ID: RPI-RP2"
for RP2040, "Board-ID: RP2350" for RP2350); no other drive is ever written to. A file whose
family does not match the board is refused.

Usage
-----
    python uf2_upload.py <file.uf2> [--port COM41]
"""

import argparse
import shutil
import string
import struct
import sys
import time
from pathlib import Path

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
UF2_FLAG_FAMILY  = 0x00002000

FAMILY_CHIP = {
    0xE48BFF56: "RP2040",
    0xE48BFF57: "RP2350",     # absolute (e.g. partition table / boot data)
    0xE48BFF58: "RP2350",     # data
    0xE48BFF59: "RP2350",     # ARM secure
    0xE48BFF5A: "RP2350",     # RISC-V
    0xE48BFF5B: "RP2350",     # ARM non-secure
}
BOARD_CHIP = {"RPI-RP2": "RP2040", "RP2350": "RP2350"}


class UploadError(Exception):
    pass


def uf2_info(path) -> dict:
    """Validate a UF2 file; returns {"chip", "families", "blocks", "bytes"}."""
    data = Path(path).read_bytes()
    if not data or len(data) % 512:
        raise UploadError(f"{Path(path).name}: not a UF2 file (size {len(data)} is not a multiple of 512)")
    families, payload = set(), 0
    for off in range(0, len(data), 512):
        m0, m1, flags, _addr, size, _bno, _nblk, fam = struct.unpack_from("<8I", data, off)
        end = struct.unpack_from("<I", data, off + 508)[0]
        if (m0, m1, end) != (UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_MAGIC_END):
            raise UploadError(f"{Path(path).name}: bad UF2 block at offset {off}")
        if flags & UF2_FLAG_FAMILY:
            families.add(fam)
        payload += size
    chips = {FAMILY_CHIP.get(f, f"unknown 0x{f:08X}") for f in families}
    if len(chips) != 1:
        raise UploadError(f"{Path(path).name}: unexpected chip families {sorted(chips)}")
    return {"chip": chips.pop(), "families": families, "blocks": len(data) // 512, "bytes": payload}


def boot_drives() -> list:
    """Mounted RP boot drives as [(root Path, chip)]."""
    found = []
    roots = ([Path(f"{c}:/") for c in string.ascii_uppercase] if sys.platform == "win32"
             else [p for base in ("/Volumes", "/media", f"/media/{Path.home().name}", "/run/media")
                   for p in Path(base).glob("*") if p.is_dir()])
    for root in roots:
        info = root / "INFO_UF2.TXT"
        try:
            if not info.is_file():
                continue
            text = info.read_text(errors="replace")
        except OSError:
            continue
        board = next((ln.split(":", 1)[1].strip() for ln in text.splitlines()
                      if ln.startswith("Board-ID:")), "")
        found.append((root, BOARD_CHIP.get(board, board or "unknown")))
    return found


def reset_to_bootloader(port: str, timeout: float = 5.0):
    """Ask the running firmware to reboot into BOOTSEL (1200-baud touch).

    The port is retried for `timeout` seconds: a program that has just closed it (the
    workbench releases its connection before uploading) may not have let go of it yet.
    If the port disappears meanwhile, the board is already restarting into the boot
    loader, and the reset counts as done."""
    import serial
    import serial.tools.list_ports
    t0 = time.time()
    while True:
        if port not in (p.device for p in serial.tools.list_ports.comports()):
            return
        try:
            ser = serial.Serial()
            ser.port = port
            ser.open()
            ser.baudrate = 9600
            ser.dtr = True
            time.sleep(0.1)
            ser.dtr = False
            ser.baudrate = 1200
            ser.close()
            return
        except (serial.SerialException, OSError) as e:
            if time.time() - t0 > timeout:
                raise UploadError(f"cannot open {port}: {e}")
            time.sleep(0.25)


def upload(path, port: str = None, log=print, timeout: float = 15.0) -> Path:
    """Upload `path`; returns the boot drive used. Raises UploadError."""
    path = Path(path)
    info = uf2_info(path)
    log(f"{path.name}: {info['blocks']} blocks, {info['bytes'] / 1024:.0f} kB, built for {info['chip']}")

    drives = boot_drives()
    if not drives:
        if not port:
            raise UploadError("no boot drive found and no serial port given — hold BOOT and press "
                              "RESET on the board, or select its serial port")
        log(f"resetting {port} into the boot loader ...")
        reset_error = None
        try:
            reset_to_bootloader(port)
        except UploadError as e:
            reset_error = e                 # the board may have reset all the same
        t0 = time.time()
        while time.time() - t0 < timeout and not drives:
            time.sleep(0.5)
            drives = boot_drives()
        if not drives:
            raise UploadError((f"{reset_error}; " if reset_error else "")
                              + f"no boot drive appeared within {timeout:.0f} s — hold BOOT and "
                              "press RESET on the board, then try again")
    if len(drives) > 1:
        raise UploadError("more than one boot drive mounted: " + ", ".join(str(d) for d, _ in drives))

    root, chip = drives[0]
    log(f"boot drive {root} ({chip})")
    if chip != info["chip"]:
        raise UploadError(f"the file is built for {info['chip']}, but the board is an {chip}; "
                          "nothing was written")
    log("copying ...")
    try:
        shutil.copyfile(path, root / path.name)
    except OSError as e:
        # the board may restart (and the drive vanish) as soon as the last block arrives
        if root.exists():
            raise UploadError(f"copy failed: {e}")
    log("upload OK — the board restarts with the new firmware")
    return root


def main():
    ap = argparse.ArgumentParser(description="Upload a .uf2 firmware file to an RP2040/RP2350 board.")
    ap.add_argument("file", help=".uf2 firmware file")
    ap.add_argument("--port", help="serial port of the running board (e.g. COM41); "
                                   "not needed if the board is already in BOOTSEL mode")
    args = ap.parse_args()
    try:
        upload(args.file, args.port)
    except UploadError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
