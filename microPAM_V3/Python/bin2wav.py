"""
bin2wav.py — convert compressed microPAM recordings (.bin, PROC_MODE 1) to WAV
==============================================================================
The output is what the firmware writes in PROC_MODE 0: the original header (fmt and
LIST/INFO chunks, so the microPAM tools still read uid, gain and times) followed by
32-bit PCM. The decoded samples are shifted back left by the file's SHIFT, so that
full scale is again 2^31 as in a raw WAV, and IKEY is changed to SHIFT 0 and
PROC_MODE 1 -> 0. Only the dropped low bits are zero.

Records missing from the .bin (ICMT "missed: acq") are filled with zeros, so that the
WAV keeps the sample timing. Each record carries the micros() of its acquisition, and
the records follow each other by T = frames per record / fs (1333.33 us for 256 frames
at 192 kHz) within about 2 %, so round(dt/T) - 1 records are missing between two of
them. Legacy (pre-3.3.0) records carry only millis(), too coarse for this; their gaps
stay closed.

Folders are converted with their subfolders; the output keeps the folder tree below
the selected folder, inside the destination folder.

Usage
-----
    python Python/bin2wav.py DEST SRC [SRC ...]      # SRC: .bin files or folders
"""

import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from micropam_browser import _safe_parse, load_samples      # noqa: E402

IKEY_SHIFT, IKEY_PROC_MODE = 7, 8       # field indices in IKEY (V5 and legacy alike)


def plan(sources, dest: Path) -> list:
    """(src, dst) pairs for the .bin files among `sources`: a file goes to dest/name.wav,
    a folder's files to dest/<folder name>/<path below the folder>.wav."""
    pairs = []
    for src in map(Path, sources):
        if src.is_dir():
            for p in sorted(src.rglob("*.bin")):
                pairs.append((p, dest / src.name / p.relative_to(src).with_suffix(".wav")))
        elif src.suffix.lower() == ".bin":
            pairs.append((src, dest / src.with_suffix(".wav").name))
    return pairs


def _set_field(text: str, idx: int, value: int) -> str:
    """Replace one ';'-separated IKEY field, keeping its width (the header is fixed)."""
    parts = text.split(";")
    if idx < len(parts):
        width = len(parts[idx])
        parts[idx] = str(value).rjust(width)[-width:] if width else str(value)
    return ";".join(parts)


def _wav_header(raw: bytes, info: dict, nbytes: int) -> bytes:
    """The source header with RIFF and data sizes for `nbytes` of samples, and IKEY
    set to SHIFT 0 and PROC_MODE 0."""
    hdr = bytearray(raw[:info["header_bytes"]])
    pos = hdr.find(b"IKEY", 36)
    if pos > 0:
        n = struct.unpack_from("<I", hdr, pos + 4)[0]
        text = bytes(hdr[pos + 8: pos + 8 + n]).rstrip(b"\x00").decode("ascii")
        text = _set_field(_set_field(text, IKEY_SHIFT, 0), IKEY_PROC_MODE, 0)
        new = text.encode("ascii")[:n]
        hdr[pos + 8: pos + 8 + n] = new + b"\x00" * (n - len(new))
    struct.pack_into("<I", hdr, 4, len(hdr) - 8 + nbytes)       # RIFF size
    struct.pack_into("<I", hdr, len(hdr) - 4, nbytes)           # data size
    return bytes(hdr)


MAX_GAP_S = 600     # a longer jump of micros() is taken as corrupt, not as missed records


def fill_gaps(data: np.ndarray, micros: list, fs: int) -> tuple:
    """Insert zero records where micros() shows records missing; returns (data, number
    of records filled). Data whose record count does not match `micros` is returned
    unchanged."""
    nrec = len(micros)
    if nrec < 2 or len(data) % nrec:
        return data, 0
    fpr = len(data) // nrec
    T = fpr / fs * 1e6
    dt = np.diff(np.asarray(micros, dtype=np.int64)) % 2**32      # micros() wraps at 2^32
    gaps = np.round(dt / T).astype(np.int64) - 1
    gaps[(gaps < 0) | (dt > MAX_GAP_S * 1e6)] = 0
    nfill = int(gaps.sum())
    if nfill == 0:
        return data, 0
    pos = np.arange(nrec) + np.concatenate(([0], np.cumsum(gaps)))  # record slots in output
    out = np.zeros((nrec + nfill, fpr, data.shape[1]), dtype=data.dtype)
    out[pos] = data.reshape(nrec, fpr, -1)
    return out.reshape(-1, data.shape[1]), nfill


def convert_file(src: Path, dst: Path) -> tuple:
    """Decode one .bin and write it as WAV with missed records zero-filled; returns
    (number of frames, number of records filled)."""
    info = _safe_parse(src)
    if info is None or not info.get("riff_ok"):
        raise ValueError("no microPAM header")
    if info.get("proc_mode") != 1:
        raise ValueError("not a compressed (PROC_MODE 1) file")
    result = load_samples(src, info)
    data, nfill = fill_gaps(result["data"], result["micros"], info["sample_rate"])
    shift = info.get("shift") or 0
    if info.get("legacy") or not info.get("ikey_ok"):
        shift = shift or 12                 # SHIFT of every compressing firmware so far
    pcm = (data.astype(np.int32) << shift).astype("<i4").tobytes()
    with open(src, "rb") as f:
        raw = f.read(info["header_bytes"])
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_suffix(".part")
    with open(tmp, "wb") as f:
        f.write(_wav_header(raw, info, len(pcm)))
        f.write(pcm)
    tmp.replace(dst)
    return len(data), nfill


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    pairs = plan(argv[1:], Path(argv[0]))
    for k, (src, dst) in enumerate(pairs, 1):
        try:
            n, nfill = convert_file(src, dst)
            print(f"{k}/{len(pairs)} {dst}  {n} frames, {nfill} missed records zero-filled")
        except Exception as e:
            print(f"{k}/{len(pairs)} {src}: {e}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
