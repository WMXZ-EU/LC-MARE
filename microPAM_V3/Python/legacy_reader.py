"""Reader for legacy microPAM_V3 recordings (adalogger_V3 firmware up to 3.1.x).

Before 3.3.0 the firmware wrote its own IKEY layout and compressed-record header.
reader.py is an unmodified copy of microPAM_V5's reader and is kept that way, so the
two differences are handled here:

* IKEY: uid; t_acq; t_on; t_rep; fsamp_khz; again; vsens; SHIFT; PROC; NBUF_ACQ; MD;
  h0; h1; h2; h3[; Version] -- no NAVG field, so the V5 parser shifts h_rec by one place.
* Compressed record: [0xA5A5A5A5, millis, nb, nd, first sample per channel, nd packed
  words], zero-padded to a multiple of 128 words. The bit packing is that of V5; only
  the header differs (4 words with millis instead of 5 with unix_s and micros).

Raw (mode 0) legacy files decode with reader.MicroPAMFile; only their IKEY needs fixing.
"""

from pathlib import Path

import numpy as np

from reader import _unpack_bits, _sign_extend

LEGACY_MAGIC = np.uint32(0xA5A5A5A5)
_HDR_LEGACY  = 4        # words: magic, millis, nb, nd
_V5_IKEY_PARTS = 17     # 16 numeric fields + Version


def _first_word(path: Path, info: dict) -> int:
    with open(path, "rb") as fh:
        fh.seek(info.get("header_bytes", 512))
        raw = fh.read(4)
    return int.from_bytes(raw, "little") if len(raw) == 4 else -1


def fix_legacy_info(path: Path, info: dict) -> dict:
    """Detect a legacy file and correct what the V5 parser read from it, in place.
    Sets info["legacy"]. A file is legacy if its IKEY has fewer fields than V5's, or,
    without IKEY (not closed cleanly), if its data stream starts with the legacy magic."""
    chunks = info.get("chunks", {})
    parts = [p.strip(" .") for p in chunks.get("IKEY", "").split(";")]
    if info.get("ikey_ok"):
        legacy = len(parts) < _V5_IKEY_PARTS
    else:
        legacy = _first_word(path, info) == LEGACY_MAGIC
    info["legacy"] = legacy
    if not legacy:
        return info

    if info.get("ikey_ok"):
        def _int(idx, default=0):
            try:    return int(parts[idx])
            except (IndexError, ValueError): return default
        info["nblks_per_disk"] = _int(10)
        info["navg"]  = "n/a"
        info["h_rec"] = [_int(11), _int(12), _int(13), _int(14)]
        info["version"] = parts[15] if len(parts) > 15 else ""
    else:
        # recover_stream_info() looked for the V5 magic and found none
        info["proc_mode"] = 1
        info["nbuf_proc"] = 1024
        info["recovered"] = True
    if not info.get("version"):
        # 3.0.x left Version out of IKEY; ISFT reads e.g. "Adalogger_V3a:3.0.2"
        isft = chunks.get("ISFT", "")
        info["version"] = isft.split(":", 1)[1] if ":" in isft else ""
    return info


def read_legacy(path: Path, info: dict) -> dict:
    """Decode a legacy mode-1 file. Returns the keys of MicroPAMFile.read_all() that the
    viewer uses ("data" as (N, nch) int32, "unix" and "micros" empty) plus "millis",
    the per-record millis() stamps, which are all the timing a legacy record carries."""
    nch = info.get("n_channels") or 1
    nbuf = info.get("nbuf_proc") or 1024
    words = np.fromfile(path, dtype="<u4", offset=info.get("header_bytes", 512))
    # compressData() pads every record to a multiple of 128 words and the stream starts
    # 512-byte aligned, so a genuine magic sits on a 128-word boundary only
    starts = np.flatnonzero(words[::128] == LEGACY_MAGIC) * 128
    frames, millis = [], []
    for pos in starts:
        if pos + _HDR_LEGACY + nch > len(words):
            break
        nb, nd = int(words[pos + 2]), int(words[pos + 3])
        end = pos + _HDR_LEGACY + nch + nd
        if not (3 <= nb <= 25) or nd <= 0 or end > len(words):
            continue
        first = words[pos + _HDR_LEGACY: pos + _HDR_LEGACY + nch].view(np.int32)
        # nd*32//nb overshoots the NBUF_ACQ packed values by up to 31//nb padding bits
        n_diffs = min(nbuf, (nd * 32) // nb) // nch * nch
        diffs = _sign_extend(_unpack_bits(words[pos + _HDR_LEGACY + nch: end], nb, n_diffs), nb)
        # the packed stream begins with an all-zero difference frame (encodeData zeroes
        # tempData[0..nch-1]), so the cumulative sum starts at the first sample itself
        frames.append(first + np.cumsum(diffs.reshape(-1, nch), axis=0, dtype=np.int32))
        millis.append(int(words[pos + 1]))
    data = np.concatenate(frames) if frames else np.empty((0, nch), dtype=np.int32)
    return {"proc_mode": 1, "data": data, "unix": [], "micros": [], "millis": millis,
            "info": info}
