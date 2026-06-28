"""
micropam_reader.py - microPAM_V4 file reader and decompressor
==============================================================

Reads any microPAM_V4 data file (.wav / .bin / .spc / .int / .vae) by
extracting acquisition parameters from the 512-byte WAV header that all
files share, then dispatching to the correct decoder based on PROC_MODE
found in the IKEY info chunk.

File extensions and their PROC_MODE:
  .wav   0   raw PCM int32 (standard WAV, no compression)
  .bin   1   integer-compressed raw samples
  .spc   2   integer-compressed per-bin FFT magnitude spectrum
  .int   3   integer-compressed directional intensity vectors
  .vae   4   VAE result vector (detection excess, scores, latent means, I)

WAV header layout (512 bytes, all fields little-endian):
  offset  0   "RIFF"
  offset  4   rLen  (updated at close)
  offset  8   "WAVE"
  offset 12   "fmt "
  offset 16   fLen = 16
  offset 20   nFormatTag = 1 (PCM)
  offset 22   nChannels              <- NCHAN_PROC
  offset 24   nSamplesPerSec         <- fsamp
  offset 28   nAvgBytesPerSec
  offset 30   nBlockAlign
  offset 34   nBitsPerSamples = 32   <- MBIT
  offset 36   "LIST"
  offset 40   lLen
  offset 44   "INFO"
  offset 48.. sub-chunks: ISFT IGNR ISRC ICMS IART IPRD ISBJ INAM ICRD IKEY ICMT
  offset 508  "data"
  offset 512  data start             <- 512 == sizeof(HdrStruct)

IKEY sub-chunk value (semicolon-separated, written by wavHeaderUpdate):
  uid; t_acq; t_on; t_rep; fsamp_khz; again; vsens; SHIFT; PROC_MODE;
  NBUF_PROC; nblks_per_disk; NAVG; h_rec[0]; h_rec[1]; h_rec[2]; h_rec[3]; Version.

Compressed record layout (modes 1/2/3, written by encodeData in process.cxx):
  [0]          0xA5A5A5A5   magic / sync word
  [1]          millis()     firmware timestamp (ms since boot)
  [2]          nb           bits per diff sample (3..25)
  [3]          nd           number of packed uint32 words that follow
  [4..4+nch-1]              first nch samples verbatim (int32 as uint32)
  [4+nch..]                 bit-packed channel-differenced samples (MSB-first)

Mode 4 record layout (written by classifier_isr in classifier.cxx):
  Header (24 x uint32, every frame):
    [0]     0x55555555   magic
    [1]     millis()
    [2]     signal_flag  (0 or 1)
    [3]     detection_excess = Dsnr - DETECT_THR  (float32 bits)
    [4..7]  vae_detect[4]                         (float32 bits)
    [8..23] vae_mu[16]                            (float32 bits)
  Signal tail (1536 x uint32, only when signal_flag == 1):
    I[3*NSAMP] interleaved as I[comp + 3*bin], comp = {x, y, z}  (float32 bits)

Usage
-----
    from micropam_reader import MicroPAMFile

    with MicroPAMFile("recording.int") as f:
        print(f.info)
        for block in f.iter_blocks():
            data = block["data"]

    result = MicroPAMFile("recording.bin").read_all()
"""

import struct
import sys
import numpy as np
from pathlib import Path

HEADER_BYTES = 512      # sizeof(HdrStruct) in filing.cxx

# Compressed-record magic (modes 1/2/3)
MAGIC        = np.uint32(0xA5A5A5A5)
_HDR_COMP    = 4        # words: magic, millis, nb, nd

# Mode 4 constants (must match classifier.h / classifier.cxx)
VAE_NVAE      = 4
VAE_LAT_TOTAL = 16      # VAE_NVAE * VAE_LAT (4*4)
NSAMP         = 512     # FFT bins = NBUF_I2S / NCHAN_ACQ
MAGIC_MODE4   = np.uint32(0x55555555)
HDR_WORDS     = 24
I_WORDS       = 3 * NSAMP   # 1536


# ── compression helpers ────────────────────────────────────────────────────────

def _unpack_bits(packed_u32: np.ndarray, nb: int, n_diffs: int) -> np.ndarray:
    """Unpack n_diffs values of nb bits each from packed_u32 (MSB-first)."""
    if n_diffs == 0 or len(packed_u32) == 0:
        return np.empty(0, dtype=np.uint32)
    be_bytes = packed_u32.astype(">u4").view(np.uint8)
    bits     = np.unpackbits(be_bytes)
    bits     = bits[: n_diffs * nb].reshape(n_diffs, nb)
    powers   = (np.uint64(1) << np.arange(nb - 1, -1, -1, dtype=np.uint64)).astype(np.uint32)
    return (bits.astype(np.uint32) * powers).sum(axis=1, dtype=np.uint32)


def _sign_extend(values: np.ndarray, nb: int) -> np.ndarray:
    """Sign-extend nb-bit unsigned integers to int32."""
    sign_bit = np.uint32(1 << (nb - 1))
    out = values.astype(np.int32)
    out[values >= sign_bit] -= np.int32(1 << nb)
    return out


def decode_record(words: np.ndarray, nch: int, ndat: int = 0) -> tuple:
    """
    Decode one compressed record starting at the magic word.

    Parameters
    ----------
    words : uint32 ndarray (at least _HDR_COMP + nch + nd elements)
    nch   : number of interleaved channels
    ndat  : expected total samples (0 = infer from nb/nd)

    Returns
    -------
    samples : int32 ndarray, shape (ndat,) interleaved  ->  reshape(-1, nch)
    millis  : int
    """
    if words[0] != MAGIC:
        raise ValueError(f"Bad magic: 0x{words[0]:08X}")
    millis = int(words[1])
    nb     = int(words[2])
    nd     = int(words[3])
    if nb < 3 or nb > 25:
        raise ValueError(f"Implausible nb={nb}")

    first       = words[_HDR_COMP : _HDR_COMP + nch].view(np.int32).copy()
    n_diffs_max = (nd * 32) // nb
    n_diffs     = (ndat - nch) if ndat > nch else n_diffs_max

    packed = words[_HDR_COMP + nch : _HDR_COMP + nch + nd]
    diffs  = _sign_extend(_unpack_bits(packed, nb, n_diffs), nb)

    nframes  = n_diffs // nch
    diffs_2d = diffs[: nframes * nch].reshape(nframes, nch)
    out      = np.empty((nframes + 1, nch), dtype=np.int32)
    out[0]   = first
    out[1:]  = first + np.cumsum(diffs_2d, axis=0)
    return out.ravel(), millis


# ── WAV / LIST-INFO parser ─────────────────────────────────────────────────────

def _parse_wav_header(raw: bytes) -> dict:
    """
    Parse the 512-byte WAV header written by wavHeaderInit / wavHeaderUpdate.

    Returns a dict with keys:
      riff_ok, wave_ok, n_channels, sample_rate, bits_per_sample,
      data_len, chunks (dict of INFO sub-chunk tag -> str value),
      proc_mode, nbuf_proc, shift, navg, fsamp_khz, again, vsens,
      t_acq, t_on, t_rep, uid, version, h_rec
    """
    if len(raw) < HEADER_BYTES:
        raise ValueError(f"Header too short: {len(raw)} bytes (need {HEADER_BYTES})")

    result = {}

    result["riff_ok"] = raw[0:4] == b"RIFF"
    result["wave_ok"] = raw[8:12] == b"WAVE"
    result["fmt_ok"]  = raw[12:16] == b"fmt "

    (fmt_len, format_tag, n_channels, sample_rate,
     avg_bytes, block_align, bits_per_sample) = struct.unpack_from("<IHHIIHH", raw, 16)

    result["n_channels"]      = n_channels
    result["sample_rate"]     = sample_rate
    result["bits_per_sample"] = bits_per_sample

    # LIST/INFO chunk starts at offset 36 (immediately after fmt chunk)
    chunks = {}
    pos = 36
    if raw[pos:pos+4] == b"LIST":
        list_len = struct.unpack_from("<I", raw, pos + 4)[0]
        list_end = pos + 8 + list_len
        if raw[pos + 8 : pos + 12] == b"INFO":
            pos += 12
            while pos + 8 <= list_end and pos + 8 <= HEADER_BYTES - 8:
                tag = raw[pos:pos + 4]
                if tag == b"data" or tag == b"\x00\x00\x00\x00":
                    break
                chunk_len   = struct.unpack_from("<I", raw, pos + 4)[0]
                value_bytes = raw[pos + 8 : pos + 8 + chunk_len]
                chunks[tag.decode("ascii", errors="replace")] = \
                    value_bytes.rstrip(b"\x00").decode("ascii", errors="replace")
                pos += 8 + chunk_len
    result["chunks"] = chunks

    # "data" marker is always at offset 508 in the fixed 512-byte header
    if raw[508:512] == b"data":
        result["data_len"] = struct.unpack_from("<I", raw, 512 - 4)[0]
    else:
        result["data_len"] = 0

    # IKEY: uid; t_acq; t_on; t_rep; fsamp_khz; again; vsens; SHIFT; PROC_MODE;
    #       NBUF_PROC; nblks; NAVG; h0; h1; h2; h3; Version.
    ikey  = chunks.get("IKEY", "")
    parts = [p.strip(" .") for p in ikey.split(";")]

    def _int(idx, default=0):
        try:    return int(parts[idx])
        except: return default

    result["uid"]       = parts[0] if parts else ""
    result["t_acq"]     = _int(1)
    result["t_on"]      = _int(2)
    result["t_rep"]     = _int(3)
    result["fsamp_khz"] = _int(4)
    result["again"]     = _int(5)
    result["vsens"]     = _int(6)
    result["shift"]     = _int(7)
    result["proc_mode"] = _int(8)
    result["nbuf_proc"] = _int(9)
    result["navg"]      = _int(11)
    result["h_rec"]     = [_int(12), _int(13), _int(14), _int(15)]
    result["version"]   = parts[16].rstrip(".") if len(parts) > 16 else ""

    return result


# ── block decoders ─────────────────────────────────────────────────────────────

def _is_compressed(proc_mode: int) -> bool:
    return proc_mode in (1, 2, 3)


def _decode_raw_pcm(data_u32: np.ndarray, nch: int) -> np.ndarray:
    samples = data_u32.view(np.int32)
    nframes = len(samples) // nch
    return samples[: nframes * nch].reshape(nframes, nch)


def _decode_compressed_block(data_u32: np.ndarray, nch: int,
                              nbuf_proc: int) -> list:
    """Scan for MAGIC records and decode each one; return list of (array, millis)."""
    records   = []
    magic_pos = np.flatnonzero(data_u32 == MAGIC)
    for pos in magic_pos:
        if pos + 4 > len(data_u32):
            break
        nb = int(data_u32[pos + 2])
        nd = int(data_u32[pos + 3])
        if nb < 3 or nb > 25:
            continue
        end = pos + 4 + nch + nd
        if end > len(data_u32):
            break
        try:
            samples, ms = decode_record(data_u32[pos:end], nch, ndat=nbuf_proc)
            records.append((samples.reshape(-1, nch), ms))
        except Exception:
            continue
    return records


def _decode_mode4_block(data_u32: np.ndarray) -> list:
    """Scan for MAGIC_MODE4 records; return list of dicts."""
    records = []
    pos     = 0
    n       = len(data_u32)
    while pos < n:
        if data_u32[pos] != MAGIC_MODE4:
            pos += 1
            continue
        if pos + HDR_WORDS > n:
            break
        hdr         = data_u32[pos : pos + HDR_WORDS]
        signal_flag = int(hdr[2])
        det_excess  = float(hdr[3:4].view(np.float32)[0])
        vae_detect  = hdr[4:8].view(np.float32).copy()
        vae_mu      = hdr[8:24].view(np.float32).copy()
        pos        += HDR_WORDS

        I_mat = None
        if signal_flag:
            if pos + I_WORDS > n:
                break
            raw_I = data_u32[pos : pos + I_WORDS].view(np.float32)
            I_mat = raw_I.copy().reshape(NSAMP, 3).T   # (3, NSAMP)
            pos  += I_WORDS

        records.append(dict(
            millis           = int(hdr[1]),
            signal_flag      = signal_flag,
            detection_excess = np.float32(det_excess),
            vae_detect       = vae_detect,
            vae_mu           = vae_mu,
            I                = I_mat,
        ))
    return records


# ── main reader class ──────────────────────────────────────────────────────────

class MicroPAMFile:
    """
    Reader for any microPAM_V4 data file (.wav / .bin / .spc / .int / .vae).

    Parameters
    ----------
    path        : str or Path
    block_words : uint32 words per disk block (default 16384 = NBUF_DISK on T4.1/RP2350)
    """

    DISK_BLOCK_WORDS_DEFAULT = 16384

    def __init__(self, path, block_words: int = 0):
        self.path         = Path(path)
        self._fh          = None
        self._block_words = block_words or self.DISK_BLOCK_WORDS_DEFAULT
        raw_header        = self.path.read_bytes()[:HEADER_BYTES]
        self.info         = _parse_wav_header(raw_header)
        pm = self.info["proc_mode"]
        if pm not in (0, 1, 2, 3, 4):
            raise ValueError(f"Unknown PROC_MODE {pm} in IKEY")

    def __enter__(self):
        self._fh = open(self.path, "rb")
        self._fh.seek(HEADER_BYTES)
        return self

    def __exit__(self, *_):
        if self._fh:
            self._fh.close()
            self._fh = None

    def iter_blocks(self):
        """
        Iterate over decoded data blocks.

        Yields dicts with "proc_mode", "millis", "data" (and extra keys for mode 4).
        Mode 4 extra keys: signal_flag, detection_excess, vae_detect, vae_mu, I.
        "data" is an alias for vae_mu in mode 4.
        """
        pm        = self.info["proc_mode"]
        nch       = self.info["n_channels"]
        nbuf_proc = self.info["nbuf_proc"]
        bw        = self._block_words

        own_fh = self._fh is None
        fh     = open(self.path, "rb") if own_fh else self._fh
        try:
            fh.seek(HEADER_BYTES)
            while True:
                raw = fh.read(bw * 4)
                if not raw:
                    break
                if len(raw) < bw * 4:
                    raw = raw + b"\x00" * (bw * 4 - len(raw))
                data_u32 = np.frombuffer(raw, dtype="<u4")

                if pm == 0:
                    yield {"proc_mode": pm, "millis": [],
                           "data": _decode_raw_pcm(data_u32, nch)}

                elif _is_compressed(pm):
                    recs = _decode_compressed_block(data_u32, nch, nbuf_proc)
                    if recs:
                        yield {"proc_mode": pm,
                               "millis": [r[1] for r in recs],
                               "data":   np.concatenate([r[0] for r in recs], axis=0)}

                elif pm == 4:
                    recs = _decode_mode4_block(data_u32)
                    if not recs:
                        continue
                    N      = len(recs)
                    I_arr  = np.zeros((N, 3, NSAMP), dtype=np.float32)
                    for i, r in enumerate(recs):
                        if r["I"] is not None:
                            I_arr[i] = r["I"]
                    mu_arr = np.stack([r["vae_mu"] for r in recs])
                    yield {
                        "proc_mode":        pm,
                        "millis":           [r["millis"]           for r in recs],
                        "signal_flag":      np.array([r["signal_flag"]      for r in recs], dtype=np.int8),
                        "detection_excess": np.array([r["detection_excess"] for r in recs], dtype=np.float32),
                        "vae_detect":       np.stack([r["vae_detect"]       for r in recs]),
                        "vae_mu":           mu_arr,
                        "I":                I_arr,
                        "data":             mu_arr,
                    }
        finally:
            if own_fh:
                fh.close()

    def read_all(self) -> dict:
        """Read the entire file and return concatenated arrays."""
        pm = self.info["proc_mode"]

        if pm != 4:
            arrays, millis = [], []
            for block in self.iter_blocks():
                arrays.append(block["data"])
                millis.extend(block["millis"])
            data = np.concatenate(arrays, axis=0) if arrays else \
                   np.empty((0, self.info["n_channels"]), dtype=np.int32)
            return {"proc_mode": pm, "data": data, "millis": millis, "info": self.info}

        keys4 = ("signal_flag", "detection_excess", "vae_detect", "vae_mu", "I")
        accum  = {k: [] for k in keys4}
        millis = []
        for block in self.iter_blocks():
            millis.extend(block["millis"])
            for k in keys4:
                accum[k].append(block[k])

        result = {"proc_mode": pm, "millis": millis, "info": self.info}
        for k in keys4:
            result[k] = np.concatenate(accum[k], axis=0) if accum[k] else \
                        np.empty(0, dtype=np.float32)
        result["data"] = result["vae_mu"]
        return result


# ── command-line quick-check ───────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python micropam_reader.py <file> [block_words]")
        sys.exit(1)

    path = sys.argv[1]
    bw   = int(sys.argv[2]) if len(sys.argv) > 2 else 0

    f   = MicroPAMFile(path, block_words=bw)
    nfo = f.info
    print(f"File   : {path}")
    print(f"UID    : {nfo['uid']}")
    print(f"Mode   : {nfo['proc_mode']}  "
          f"({'compressed' if _is_compressed(nfo['proc_mode']) else 'raw'})")
    print(f"fsamp  : {nfo['sample_rate']} Hz  nch={nfo['n_channels']}  "
          f"bits={nfo['bits_per_sample']}")
    print(f"SHIFT  : {nfo['shift']}  NBUF_PROC={nfo['nbuf_proc']}  NAVG={nfo['navg']}")
    print(f"Version: {nfo['version']}")
    print()

    result = f.read_all()
    pm     = result["proc_mode"]

    if pm != 4:
        data = result["data"]
        print(f"Decoded: shape={data.shape}  dtype={data.dtype}")
        if data.size:
            print(f"         min={data.min()}  max={data.max()}")
        if result["millis"]:
            ms = result["millis"]
            print(f"         {len(ms)} records  first={ms[0]} ms  last={ms[-1]} ms")
    else:
        N     = len(result["signal_flag"])
        n_sig = int(result["signal_flag"].sum())
        print(f"Decoded: {N} frames  ({n_sig} signal, {N - n_sig} noise)")
        print(f"  vae_mu     shape={result['vae_mu'].shape}")
        print(f"  vae_detect shape={result['vae_detect'].shape}")
        print(f"  I          shape={result['I'].shape}")
        if result["millis"]:
            ms = result["millis"]
            print(f"  millis     first={ms[0]}  last={ms[-1]}")
        if n_sig:
            exc = result["detection_excess"][result["signal_flag"] == 1]
            print(f"  det_excess on signal: mean={exc.mean():.3f}")
