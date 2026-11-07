"""
micropam_reader.py - microPAM_V5 file reader and decompressor
=============================================================

Reads a microPAM_V5 data file (.wav raw PCM or .bin compressed) by extracting
acquisition parameters from the WAV header that both files share, then
decoding the audio stream. The header is a multiple of 512 bytes (512 baseline,
or 1024 when the DCL overlay is built, so the DCLH meta chunk fits in the header
tail); either way the audio data starts on a 512-byte boundary. V5 keeps only
two processing modes; the compressed
frame header carries a CVAS3-style unit time (unix seconds + free-running
micros) in place of V4's boot-relative millis (see Docs/Processing_Modes.md).

**This decoder reads V5 files only.** The V5 record magic words and header word
order differ from every earlier firmware, so V4x recordings (and their .spc /
.int / .vae processing modes, which V5 does not have) will not decode here —
read those with the V4x project's own decoder. Failure is clean rather than
silent: the magic scan finds nothing, and a V4x PROC_MODE is rejected outright
by MicroPAMFile.

Two kinds of record are interleaved into a V5 file and stripped out
transparently: NAD blocks (non-acoustic data: compass/IMU + MCU temperature,
Docs/NAD_Module.md), and — when the firmware was built with the opt-in DCL
overlay (USE_DCL) — combined DET5 detection/classification/localization records
(Docs/DCL_Processing_Chain.md). MicroPAMFile collects them into `nad_blocks` /
`det_events` and exposes merged arrays via the "nad" / "events" keys of
read_all(). This module also reads the two file types written by the standalone
microPAM_calibrate bench tool (Teensy 4.1 only, own src/ tree, see
Docs/NAD_Calibration_Tool.md):
  NAD_CALIB.dat          fitted CalibResult struct -> read_nad_calib_result()
  NAD_CALIB_<epoch>.bin  raw buffered NAD blocks, no WAV header,
                         variable-length last block -> read_nad_calib_stream()

File extensions and their PROC_MODE:
  .wav   0   raw PCM int32 (standard WAV, no compression)
  .bin   1   integer-compressed raw samples (+ interleaved DET5 if USE_DCL)

WAV header layout (H = header_bytes: 512, or 1024 with the DCL overlay; all
fields little-endian):
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
  offset 40   lLen = H - 52          <- LIST content runs up to the "data" marker
  offset 44   "INFO"
  offset 48.. sub-chunks: ISFT IGNR ISRC ICMS IART IPRD ISBJ INAM ICRD IKEY ICMT
              (+ a trailing DCLH meta chunk in the header tail on USE_DCL builds)
  offset H-8  "data"
  offset H-4  dLen (audio/NAD bytes only; the DCLH constants are not counted)
  offset H    data start             <- H == sizeof(HdrStruct), a multiple of 512

IKEY sub-chunk value (semicolon-separated, written by wavHeaderUpdate):
  uid; t_acq; t_on; t_rep; fsamp_khz; again; vsens; SHIFT; PROC_MODE;
  NBUF_PROC; nblks_per_disk; NAVG; h_rec[0]; h_rec[1]; h_rec[2]; h_rec[3]; Version.

Every record type interleaved into a recording file starts with the same 3-word
prefix, so a reader can key off the magic word and read any record's unit time
without knowing its type (src/global.h):
  [0]  magic — 4 ASCII chars, most-significant byte first ("COMP", "NAD_",
       "DET5", "DCLH"); stored little-endian like every other word
  [1]  rtc_get_unix()  unix seconds — the firmware normalises here, because the
       MCU clocks differ (rtc_get() counts from 1970 on Teensy but from 2000 on
       the RP targets); files always carry the 1970 epoch (src/rtc.h RTC_EPOCH)
  [2]  micros()   free-running microsecond counter (wraps ~71.6 min)

Compressed record layout (mode 1, written by encodeData in process.cxx). V5
carries CVAS3-style "unit time" in the header (unix seconds + free-running
micros) in place of V4's boot-relative millis():
  [0]          0x434F4D50   magic / sync word ("COMP")
  [1]          rtc_get_unix() unix seconds
  [2]          micros()     free-running microsecond counter (wraps ~71.6 min)
  [3]          nb           bits per diff sample (3..25)
  [4]          nd           number of packed uint32 words that follow
  [5..5+nch-1]              first nch samples verbatim (int32 as uint32)
  [5+nch..]                 bit-packed channel-differenced samples (MSB-first)

DCL meta chunk ("DCLH", embedded in the WAV header tail when USE_DCL is built,
src/dcl.cxx dcl_get_header / src/filing.cxx wavHeaderInit): the DCL method's
*constant* parameters — method id, AR(2) coefficients, leak/EMA time constants,
thresholds, array geometry, EM gate constants, and the elevation/CRI gate windows
the classifier resolved from the deployment envelope — so offline processing has
the fixed DCL parameters — plus, since 2026-08-08, whether the unit ran the
classify pass at all ('classify'). It rides in the header (not the data stream)
so the audio stays 512-aligned; located by magic scan. Its length has grown
several times and is how a reader tells the versions apart (see
_decode_dcl_header). Exposed as read_all()['dcl_header'].

DET5 record layout (opt-in DCL overlay, written by dcl_classify_isr in
src/dcl.cxx; see Docs/DCL_Processing_Chain.md). One combined
detection/classification/localization record is emitted per event — per
*labelled* event normally, and per detected event on a detection-only build
(DCL_CLASSIFY=0), where the class word is DCL_CLASS_NONE and the EM state is
NaN. Records are interleaved into the same mode-1 file as compressed audio and
NAD blocks,
distinguished by its leading magic word. Because DCL runs asynchronously to the
compress path, direct_us/ipi_us point each record back into the compressed audio
stream; and each record carries the frame-to-frame *resume state* so offline can
pick up the true DCL state (constants are in the DCLH chunk). Layout (DCL_NCH=4):
  [0]        0x44455435   magic ("DET5")
  [1]        rtc_get_unix() unix seconds (record emit unit time)
  [2]        micros()     record emit micros
  [3]        dcl_method   (1 = AR(2)+Teager-Kaiser, 43 words, ..2026-07-30;
                            2 = AR(2)+w[n]^2, 44 words, 2026-07-31..) — the
                            record LENGTH follows it, so read it first
  [4]        direct_us    micros() of the direct source acquisition block
  [5]        ipi_us       micros() of the previous / inter-pulse-interval block
  [6..13]    t, freq_bf, level_bf, dir[0], dir[1], dir[2], az, el (float32 bits)
  [14]       class        (uint32: 0 unassigned, 1+2*jj click / 2+2*jj
                            reflection for animal jj; 0xFFFFFFFF = the
                            classifier did not run, decoded as cls = -1)
  [15..22]   seed_xtail[ch][0..1]  AR(2) raw tails (float32 bits, resume)
  [23..30]   seed_wtail[ch][0..1]  whitened tails (float32 bits, resume)
  [31]       seed_ptrk    leaky-peak tracker (float32 bits, resume)
  [32]       seed_Lf      log noise-floor EMA (float32 bits, resume)
  [33..41]   EM state: cri_a, cri_b, log_ici_fast/slow, log_dmax_fast/slow,
             dir_click_run[0..2] (float32 bits; all NaN if no classify pass ran)
  [42]       dmax_seeded  (uint32 0/1)
  [43]       spread       4-channel whitened-peak spread [samples] (method 2+)

NAD block layout (interleaved by filing.cxx: write_nad(), and written standalone
by microPAM_calibrate's sd_io.cxx: saveNadData()); see
Docs/NAD_Module.md:
  Header (8 x uint32, one per block, 32 bytes):
    [0]     0x4E41445F   magic ("NAD_")
    [1]     rtc_get_unix() Unix seconds at block start
    [2]     micros()     free-running microsecond counter at block start
    [3]     reserved, 0
    [4]     reserved, 0
    [5]     sample count (n_records * 16, informational)
    [6..7]  reserved, 0
  Data (n_records x 16 x int16, 32 bytes/record; n_records is 95 for an
  in-firmware block, <=95 for the last block of a microPAM_calibrate raw dump):
    [0..2] accel xyz, [3..5] mag xyz, [6..8] gyro xyz, [9] accel temp,
    [10] mag temp, [11] MCU temp, [12..15] reserved/0.

Usage
-----
    from micropam_reader import MicroPAMFile

    with MicroPAMFile("recording.bin") as f:
        print(f.info)
        for block in f.iter_blocks():
            data = block["data"]
        print(f.nad_blocks)          # NAD blocks found while iterating
        print(f.det_events)          # DET5 DCL detections found while iterating

    result = MicroPAMFile("recording.bin").read_all()
    result["nad"]                    # merged NAD arrays, or None
    result["events"]                 # merged DET5 detections, or None

    calib  = read_nad_calib_result("NAD_CALIB.dat")
    blocks = read_nad_calib_stream("NAD_CALIB_1234567890.bin")
"""

import struct
import sys
import numpy as np
from pathlib import Path

HEADER_BYTES = 512      # sizeof(HdrStruct) in filing.cxx

# Every record type interleaved into a recording file starts with the same
# 3-word prefix — [0] magic, [1] rtc_get_unix() unix seconds, [2] micros() — so any
# record's unit time can be read without knowing its type (src/global.h).
# Magic words are 4 ASCII characters, most-significant byte first.

# Compressed-record magic (mode 1). V5 header: magic, unix_s, micros, nb, nd.
COMP_MAGIC   = np.uint32(0x434F4D50)   # "COMP"
_HDR_COMP    = 5        # words: magic, unix_s, micros, nb, nd

NSAMP        = 512      # FFT bins = NBUF_I2S / NCHAN_ACQ (retained for tooling)
DCL_NCH      = 4        # DCL fixed 4-channel tetrahedron

# DCL method ids (DET5 word [3] / DCLH word [3]). The method also fixes the DET5
# record LENGTH, which is why a reader must read word [3] before consuming one.
DCL_METHOD_TK  = 1   # AR(2) + Teager-Kaiser, 43-word DET5   ..2026-07-30
DCL_METHOD_AR2 = 2   # AR(2) + w[n]^2, 44-word DET5 (+spread)  2026-07-31..

# DCL constant "meta chunk" written once into each file's header region
# (src/dcl.cxx dcl_get_header, src/filing.cxx) — the fixed DCL parameters.
DCL_HEADER_MAGIC = np.uint32(0x44434C48)   # "DCLH"

# DET5 combined detection/classification/localization record (src/dcl.cxx,
# Docs/DCL_Processing_Chain.md) — interleaved into the mode-1 file. Carries the
# DCL method id + the frame-to-frame resume state (detector seed + EM state).
DET5_MAGIC   = np.uint32(0x44455435)   # "DET5"
# Words per record, by method id at [3]; DET5_WORDS is the current firmware's.
DET5_WORDS_BY_METHOD = {DCL_METHOD_TK: 43, DCL_METHOD_AR2: 44}
DET5_WORDS   = DET5_WORDS_BY_METHOD[DCL_METHOD_AR2]

# NAD (non-acoustic data) block, Teensy 4.1 only (src/nad.cxx, Docs/NAD_Module.md)
NAD_MAGIC       = np.uint32(0x4E41445F)   # "NAD_"
NAD_HDR_WORDS   = 8
NAD_HDR_BYTES   = 32
NAD_REC_BYTES   = 32     # bytes per 16 x int16 data record
NAD_REC_INT16   = 16

# microPAM_calibrate bench-tool output (microPAM_calibrate/src/sd_io.cxx,
# Docs/NAD_Calibration_Tool.md) - not written by the main firmware
NAD_CALIB_MAGIC = np.uint32(0x43414C31)   # "CAL1"

CALIB_RESULT_DTYPE = np.dtype([
    ("acc_bias",         "<f4", (3,)),
    ("acc_scale",        "<f4", (3,)),
    ("acc_rms",          "<f4"),
    ("mag_bias",         "<f4", (3,)),
    ("mag_scale_matrix", "<f4", (3, 3)),
    ("mag_rms",          "<f4"),
    ("gyro_bias",        "<f4", (3,)),
    ("gyro_scale",       "<f4", (3,)),
    ("gyro_rms",         "<f4", (3,)),
    ("rotation_deg",     "<f4", (3,)),
    ("acc_range",        "<f4", (3,)),
    ("mag_range",        "<f4", (3,)),
    ("acc_octants",      "<u4"),
    ("mag_octants",      "<u4"),
    ("n_samples",        "<u4"),
    ("sufficient",       "<u4"),
])
assert CALIB_RESULT_DTYPE.itemsize == 168, CALIB_RESULT_DTYPE.itemsize


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
    unix_s  : int   (rtc_get_unix() unix seconds from the frame header)
    micros  : int   (free-running microsecond counter from the frame header)
    """
    if words[0] != COMP_MAGIC:
        raise ValueError(f"Bad COMP magic: 0x{words[0]:08X}")
    unix_s = int(words[1])
    micros = int(words[2])
    nb     = int(words[3])
    nd     = int(words[4])
    if nb < 3 or nb > 25:
        raise ValueError(f"Implausible nb={nb}")

    # encodeData() (src/process.cxx) packs all `ndat` entries of tempData, not
    # just the differences: it stores frame 0 absolutely in the nch words after
    # the header AND zeroes tempData[0..nch-1] before encodeBlock(), so the
    # packed stream begins with one all-zero difference frame. Reconstructing it
    # therefore yields ndat/nch + 1 frames, of which the first two are the same
    # sample - the redundant one is dropped below. Taking `ndat - nch` diffs
    # here instead would silently discard the record's *last* frame.
    first       = words[_HDR_COMP : _HDR_COMP + nch].view(np.int32).copy()
    n_diffs_max = (nd * 32) // nb
    n_diffs     = ndat if ndat > nch else n_diffs_max
    n_diffs     = min(n_diffs, n_diffs_max)

    packed = words[_HDR_COMP + nch : _HDR_COMP + nch + nd]
    diffs  = _sign_extend(_unpack_bits(packed, nb, n_diffs), nb)

    nframes  = n_diffs // nch
    diffs_2d = diffs[: nframes * nch].reshape(nframes, nch)
    out      = np.empty((nframes + 1, nch), dtype=np.int32)
    out[0]   = first
    out[1:]  = first + np.cumsum(diffs_2d, axis=0)
    return out[1:].ravel(), unix_s, micros


# ── WAV / LIST-INFO parser ─────────────────────────────────────────────────────

def _parse_wav_header(raw: bytes) -> dict:
    """
    Parse the WAV header written by wavHeaderInit / wavHeaderUpdate.

    The header is a multiple of 512 bytes (512 baseline, or 1024 when the DCL
    overlay is built so the DCLH meta chunk fits in the header tail); the "data"
    marker sits at the very end so the audio stream always starts on a 512-byte
    boundary. The header size is derived from the LIST chunk length (lLen) rather
    than assumed, and returned as "header_bytes".

    Returns a dict with keys:
      riff_ok, wave_ok, header_bytes, n_channels, sample_rate, bits_per_sample,
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

    # LIST chunk starts at offset 36 (immediately after fmt chunk). Its content
    # (iId "INFO" + info[]) runs up to the trailing "data" marker, so lLen fixes
    # both the INFO walk bound and the total header size.
    header_bytes = HEADER_BYTES
    list_end     = HEADER_BYTES - 8
    if raw[36:40] == b"LIST":
        list_len     = struct.unpack_from("<I", raw, 40)[0]
        data_off     = 44 + list_len              # "data" marker sits right after LIST content
        if 0 < data_off <= len(raw) - 8 and raw[data_off:data_off + 4] == b"data":
            header_bytes = data_off + 8           # audio starts here (multiple of 512)
            list_end     = data_off
    result["header_bytes"] = header_bytes

    # LIST/INFO sub-chunk walk (text metadata), bounded by the "data" marker
    chunks = {}
    pos = 36
    if raw[pos:pos+4] == b"LIST" and raw[pos + 8 : pos + 12] == b"INFO":
        pos += 12
        while pos + 8 <= list_end:
            tag = raw[pos:pos + 4]
            if tag == b"data" or tag == b"\x00\x00\x00\x00" or tag == DCL_HEADER_MAGIC.tobytes():
                break
            chunk_len   = struct.unpack_from("<I", raw, pos + 4)[0]
            value_bytes = raw[pos + 8 : pos + 8 + chunk_len]
            chunks[tag.decode("ascii", errors="replace")] = \
                value_bytes.rstrip(b"\x00").decode("ascii", errors="replace")
            pos += 8 + chunk_len
    result["chunks"] = chunks

    # "data" marker sits at header_bytes-8 (end of the header)
    if raw[header_bytes - 8 : header_bytes - 4] == b"data":
        result["data_len"] = struct.unpack_from("<I", raw, header_bytes - 4)[0]
    else:
        result["data_len"] = 0

    # IKEY: uid; t_acq; t_on; t_rep; fsamp_khz; again; vsens; SHIFT; PROC_MODE;
    #       NBUF_PROC; nblks; NAVG; h0; h1; h2; h3; Version.
    # IKEY is patched in by wavHeaderUpdate(), which only runs where logger()
    # closes the file - on a t_acq rollover, or on the stop ('e') command. A file
    # still open when the board loses power or reboots therefore keeps every
    # open-time INFO chunk but has no IKEY. Flag that here; MicroPAMFile recovers
    # what it can from the stream itself.
    ikey  = chunks.get("IKEY", "")
    parts = [p.strip(" .") for p in ikey.split(";")]
    result["ikey_ok"]   = bool(ikey.strip())
    result["recovered"] = False

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
    result["nblks_per_disk"] = _int(10)
    result["navg"]      = _int(11)
    result["h_rec"]     = [_int(12), _int(13), _int(14), _int(15)]
    result["version"]   = parts[16].rstrip(".") if len(parts) > 16 else ""

    return result


# ── battery voltage ───────────────────────────────────────────────────────────

# IKEY field 7 ("vsens") is analogRead(A1), taken by filing.cxx when a file is
# closed and written by wavHeaderUpdate(). A1 sees the battery through a divider
# of 300 kOhm over 100 kOhm, i.e. one quarter of its voltage. analogRead() runs
# at the Arduino default resolution of 10 bits, as the firmware never calls
# analogReadResolution(), and its reference is the 3.3 V supply of the MCU.
VBAT_R_TOP    = 300e3     # Ohm, divider resistor to the battery
VBAT_R_BOTTOM = 100e3     # Ohm, divider resistor to ground
VBAT_VREF     = 3.3       # V, reference of the MCU converter
VBAT_BITS     = 10        # analogRead() default resolution


def vsens_to_volts(counts, r_top: float = VBAT_R_TOP,
                   r_bottom: float = VBAT_R_BOTTOM, vref: float = VBAT_VREF,
                   bits: int = VBAT_BITS):
    """Convert IKEY vsens counts to the battery voltage in V.

    V_bat = counts / (2**bits - 1) * vref * (r_top + r_bottom) / r_bottom.
    Accepts a scalar or an array; a count <= 0, which no powered divider can
    give (and which a file without IKEY reports), returns NaN.
    """
    n = np.asarray(counts, dtype=float)
    v = n / (2 ** bits - 1) * vref * (r_top + r_bottom) / r_bottom
    v = np.where(n > 0, v, np.nan)
    return float(v) if v.ndim == 0 else v


def header_voltage(path, **kwargs) -> float:
    """Battery voltage in V recorded in the header of a microPAM file, when the
    file was closed. Reads only the header; keyword arguments are passed on to
    vsens_to_volts() (r_top, r_bottom, vref, bits)."""
    with open(path, "rb") as fh:
        raw = fh.read(MicroPAMFile.MAX_HEADER_BYTES)
    return vsens_to_volts(_parse_wav_header(raw)["vsens"], **kwargs)


def recover_stream_info(path, info: dict, probe_words: int = 32768) -> dict:
    """Recover the decoding parameters IKEY would have carried, for a file that
    was cut short before wavHeaderUpdate() could write it (info["ikey_ok"] is
    False). Updates `info` in place and returns it.

    Only `proc_mode` and `nbuf_proc` actually matter for decoding, and both can
    be read back out of the stream, because compressed records are
    self-describing. Without this the IKEY defaults leave proc_mode at 0 and a
    compressed file decodes as raw PCM - plausible-looking, silently wrong
    samples.

    Sets info["recovered"]. What only IKEY knows (uid, gain, sensitivity, duty
    cycle, firmware version) stays at its default and cannot be recovered.
    """
    info["recovered"] = True
    # The fmt chunk is written when the file is opened, so it survived.
    info["fsamp_khz"] = info["sample_rate"] // 1000

    with open(path, "rb") as fh:
        fh.seek(info["header_bytes"])
        probe = fh.read(probe_words * 4)
    words = np.frombuffer(probe[: (len(probe) // 4) * 4], dtype="<u4")

    # compressData() pads every record out to a multiple of 128 words and the
    # data stream itself starts 512-aligned, so a genuine COMP magic can only
    # sit on a 128-word boundary - which rules out an audio sample that happens
    # to equal the magic. Finding none leaves proc_mode at its 0 (raw PCM)
    # default, the right reading of a mode-0 file truncated the same way.
    nch = info["n_channels"] or 1
    for pos in range(0, max(len(words) - _HDR_COMP, 0), 128):
        if words[pos] != COMP_MAGIC:
            continue
        nb, nd = int(words[pos + 3]), int(words[pos + 4])
        if not (3 <= nb <= 25) or nd <= 0:
            continue
        info["proc_mode"] = 1
        # encodeData() packs (NBUF_PROC - nch) differences of nb bits into nd
        # words, so nd*32//nb overshoots NBUF_PROC-nch by at most 31//nb (<= 10)
        # samples. NBUF_PROC is a power of two (NBUF or 2*NBUF), and those are
        # far enough apart that rounding down recovers it exactly.
        info["nbuf_proc"] = 1 << (((nd * 32) // nb + nch).bit_length() - 1)
        break
    return info


# ── block decoders ─────────────────────────────────────────────────────────────

def _is_compressed(proc_mode: int) -> bool:
    return proc_mode == 1


def _decode_raw_pcm(data_u32: np.ndarray, nch: int) -> np.ndarray:
    samples = data_u32.view(np.int32)
    nframes = len(samples) // nch
    return samples[: nframes * nch].reshape(nframes, nch)


def _decode_stream_block(data_u32: np.ndarray, nch: int,
                         nbuf_proc: int) -> tuple:
    """Decode the COMP and DET5 records of a mode-1 data block in stream order.

    The firmware pushes whole records into one word stream (src/process.cxx
    queue) and writes that stream out in disk blocks, so a record frequently
    runs across a block boundary. Returns (records, events, used): records is a
    list of (array, unix_s, micros), events a list of decoded DET5 dicts, and
    used the number of leading words consumed. A record starting at `used` is
    incomplete; the caller carries data_u32[used:] over into the next block.

    The search resumes after the end of each decoded record, so that a magic
    value inside packed audio is not taken for a record start. A COMP header
    with nd > nbuf_proc cannot be genuine and is skipped like any other
    implausible header."""
    records, events = [], []
    n         = len(data_u32)
    magic_pos = np.flatnonzero((data_u32 == COMP_MAGIC) | (data_u32 == DET5_MAGIC))
    next_pos  = 0
    for pos in magic_pos:
        if pos < next_pos:
            continue
        if data_u32[pos] == COMP_MAGIC:
            if pos + _HDR_COMP > n:
                return records, events, int(pos)
            nb = int(data_u32[pos + 3])
            nd = int(data_u32[pos + 4])
            if nb < 3 or nb > 25 or nd > nbuf_proc:
                continue
            end = pos + _HDR_COMP + nch + nd
            if end > n:
                return records, events, int(pos)
            try:
                samples, unix_s, micros = decode_record(data_u32[pos:end], nch,
                                                         ndat=nbuf_proc)
            except Exception:
                continue
            records.append((samples.reshape(-1, nch), unix_s, micros))
        else:
            # the DET5 record length follows the method id at [3] (src/dcl.cxx)
            if pos + 4 > n:
                return records, events, int(pos)
            nw = DET5_WORDS_BY_METHOD.get(int(data_u32[pos + 3]))
            if nw is None:
                continue                       # not a record we know how to read
            end = pos + nw
            if end > n:
                return records, events, int(pos)
            try:
                events.append(_decode_det5(data_u32[pos:end]))
            except Exception:
                continue
        next_pos = end
    return records, events, n


# ── DET5 (interleaved DCL) block decoder ────────────────────────────────────────

# DET5 word [14] when the unit ran no classify pass (src/dcl.cxx
# DCL_CLASS_NONE, global.h DCL_CLASSIFY=0). Decoded to -1, which is what
# microPAM.dcl.dcl_cxx.CLS_NONE uses for the same thing.
DET5_CLASS_NONE = 0xFFFFFFFF
CLS_NONE = -1


def _decode_det5_class(c: int) -> dict:
    """Split a DET5 class word into cls / animal / kind.

    kind is 0 noise, 1 click, 2 reflection; animal is the track index. All
    three are -1 for an unclassified event, so a caller that filters on
    ``kind == 0`` never picks one up as a rejection.
    """
    if c == DET5_CLASS_NONE:
        return dict(cls=CLS_NONE, animal=CLS_NONE, kind=CLS_NONE)
    return dict(cls=c,
                animal=((c - 1) // 2) if c else -1,
                kind=(2 - c % 2) if c else 0)


def _decode_det5(words: np.ndarray) -> dict:
    """Decode one 'DET5' combined detection record (DET5_WORDS words, starting at
    the magic). Carries the DCL method id, the per-event outputs + async
    alignment pointers, and the frame-to-frame *resume state* (detector seed
    AR/Teager tails + leaky-peak + noise-floor, and the batch EM classifier
    state) so offline processing can pick up the true DCL state."""
    if words[0] != DET5_MAGIC:
        raise ValueError(f"Bad DET5 magic: 0x{words[0]:08X}")
    f = words[6:14].view(np.float32)
    t, freq_bf, level_bf, dx, dy, dz, az, el = (float(x) for x in f)
    xtail = words[15:23].view(np.float32).reshape(DCL_NCH, 2).copy()
    wtail = words[23:31].view(np.float32).reshape(DCL_NCH, 2).copy()
    em    = words[33:42].view(np.float32)
    return dict(
        unix_s     = int(words[1]),
        micros     = int(words[2]),
        dcl_method = int(words[3]),
        direct_us  = int(words[4]),
        ipi_us     = int(words[5]),
        t=t, freq_bf=freq_bf, level_bf=level_bf,
        dir=np.array([dx, dy, dz], dtype=np.float32),
        az=az, el=el,
        # Class label: 0 = noise, else 1+2*jj (click) / 2+2*jj (reflection) for
        # animal jj. A DCL_NA=1 build only ever emits 0/1/2, so `animal` is 0
        # for every labelled event and `kind` reproduces the old meaning of
        # `cls` exactly. The EM state below is the OWNING track's — the one
        # whose gates labelled this event (src/dcl.cxx dcl_classify_isr).
        #
        # DCL_CLASS_NONE (0xFFFFFFFF) is a detection-only build (DCL_CLASSIFY=0)
        # and decodes to cls/animal/kind all -1, matching dcl_cxx.CLS_NONE. It
        # is NOT 0: nothing examined this event, so counting it as noise would
        # invent a rejection the firmware never made. The EM words are NaN.
        **_decode_det5_class(int(words[14])),
        # detector resume seed (state at the start of the detection block)
        seed_xtail=xtail, seed_wtail=wtail,
        seed_ptrk=float(words[31:32].view(np.float32)[0]),
        seed_Lf  =float(words[32:33].view(np.float32)[0]),
        # batch EM classifier resume state
        em_cri_a=float(em[0]), em_cri_b=float(em[1]),
        em_log_ici_fast=float(em[2]),  em_log_ici_slow=float(em[3]),
        em_log_dmax_fast=float(em[4]), em_log_dmax_slow=float(em[5]),
        em_dir_click_run=np.array([em[6], em[7], em[8]], dtype=np.float32),
        em_dmax_seeded=int(words[42]),
        # 4-channel whitened-peak spread [samples] — a plane wave (direct
        # arrival, or a sperm whale's internal IPI reflection) cannot spread the
        # four peaks by more than the array's transit time; a surface return off
        # anything but a flat calm sea is a facet sum and can. Method 2+ only;
        # NaN for the older records so it is never mistaken for a measured 0.
        spread=(float(words[43:44].view(np.float32)[0])
                if len(words) > 43 else float("nan")),
    )


# DCL constant meta chunk (src/dcl.cxx dcl_get_header): [0] magic, [1] unix_s,
# [2] micros, [3] method, [4] nwords, [5] nch, [6] fsamp, [7] nsamp, [8] shift,
# [9..9+2nch) AR a1/a2, then the float/uint constants (see dcl_get_header).
#
# Three versions, told apart by nwords ([4]):
#   44  ..2026-07-29  fl[12] is DCL_EL_THR_DEG, a symmetric elevation backstop;
#                     the CRI had no window at all, only the cold start fl[22].
#   49  2026-07-29..  the gates are derived from the deployment envelope
#                     (src/global.h DCL_RECV_DEPTH_M / DCL_SRC_*), fl[12] is
#                     el_dir_lo and fl[27..31] carry the rest of the windows.
#   9+2*nch+38+2*na   multi-animal: DCL_NA at [k+32], the four continuity-gate
#   (55+2*na at       values and the first-pass CRI width at fl[33..37], then
#    nch=4) since     one spectral band per animal at fl[38..38+2*na).
#    2026-07-30       na is what makes a DET5 class label readable at all.
#   9+2*nch+39+2*na   DCL method 2 (AR(2)+w^2 detector): refl_spread_min
#   (56+2*na at       appended at fl[38], the per-animal bands shifted one
#    nch=4) since     word later. Told apart from the row above exactly,
#    2026-07-31       since both forms are a fixed length given na.
#   9+2*nch+40+2*na   DCL_CLASSIFY appended at [k+39]: whether the unit ran the
#   (57+2*na at       classify pass at all. Without it an unlabelled file is
#    nch=4) since     indistinguishable from one where the classifier ran and
#    2026-08-08       assigned nothing, and every gate below is inert.
# All decode to the same keys: an older header is mapped onto the form it is
# equivalent to (+-el_thr with reflection elevation and CRI unbounded; na=1
# with every continuity gate 0 = off; refl_spread_min 0 = off; classify=1,
# since every build that wrote a shorter header had no way not to classify).
def _decode_dcl_header(words: np.ndarray) -> dict:
    if words[0] != DCL_HEADER_MAGIC:
        raise ValueError(f"Bad DCLH magic: 0x{words[0]:08X}")
    nch = int(words[5])
    nwords = int(words[4])
    ar_a1 = words[9:9+nch].view(np.float32).copy()
    ar_a2 = words[9+nch:9+2*nch].view(np.float32).copy()
    k = 9 + 2*nch
    fl = words[k:].view(np.float32)   # float view of the tail (uints re-read below)
    h = dict(
        unix_s=int(words[1]), micros=int(words[2]),
        method=int(words[3]), nwords=nwords, nch=nch,
        fsamp=int(words[6]), nsamp=int(words[7]), shift=int(words[8]),
        ar_a1=ar_a1, ar_a2=ar_a2,
        dcl_tau=float(fl[0]), dcl_tau_bg=float(fl[1]), thr_db=float(fl[2]),
        array_edge_m=float(fl[3]), c_sw=float(fl[4]),
        ici_lo=float(fl[5]), ici_hi=float(fl[6]), cri_tol=float(fl[7]),
        ici_tol=float(fl[8]), freq_thr=float(fl[9]), dmax_tol=float(fl[10]),
        dir_tol_deg=float(fl[11]),
        n_seed=int(words[k+13]), n_post_pause=int(words[k+14]),
        ici_alpha_fast=float(fl[15]), ici_alpha_slow=float(fl[16]),
        ici_beta_return=float(fl[17]), dmax_alpha_fast=float(fl[18]),
        dmax_alpha_slow=float(fl[19]), dmax_beta_return=float(fl[20]),
        dir_alpha=float(fl[21]), cri_init=float(fl[22]),
        em_n_iter=int(words[k+23]), classify_period_ms=int(words[k+24]),
        kinematic_mode=int(words[k+25]), h_recv=float(fl[26]),
    )
    if nwords >= k + 32:        # geometry-derived gate windows
        h.update(el_dir_lo=float(fl[12]),   el_dir_hi=float(fl[27]),
                 el_refl_lo=float(fl[28]),  el_refl_hi=float(fl[29]),
                 cri_lo=float(fl[30]),      cri_hi=float(fl[31]))
    else:                       # fixed +-DCL_EL_THR_DEG, no reflection windows
        el_thr = float(fl[12])
        h.update(el_thr_deg=el_thr,
                 el_dir_lo=-el_thr, el_dir_hi=+el_thr,
                 el_refl_lo=-180.0, el_refl_hi=+180.0,
                 cri_lo=0.0, cri_hi=float("inf"))
    if nwords >= k + 38:        # multi-animal tracking + the continuity gates
        na = int(words[k+32])
        # two later forms appended a word each before the per-animal bands:
        # method 2's refl_spread_min, then DCL_CLASSIFY. Each form is a fixed
        # length given na, so the bands' offset follows from the total exactly.
        spread_slot   = 1 if nwords >= k + 39 + 2*na else 0
        classify_slot = 1 if nwords >= k + 40 + 2*na else 0
        b = 38 + spread_slot + classify_slot
        h.update(na=na,
                 az_track_tol=float(fl[33]), cri_track_tol=float(fl[34]),
                 refl_el_min=float(fl[35]),  refl_az_tol=float(fl[36]),
                 cri_tol_1=float(fl[37]),
                 refl_spread_min=float(fl[38]) if spread_slot else 0.0,
                 # 0 = the unit detected but did not classify. No classifier
                 # was compiled, so every gate key above is NaN (n_seed and the
                 # other integer ones 0) and every DET5 class label in the file
                 # is CLS_NONE. `na` stays real: it sizes the bands below.
                 classify=int(words[k+39]) if classify_slot else 1,
                 freq_lo_na=fl[b:b+na].astype(float).copy(),
                 freq_thr_na=fl[b+na:b+2*na].astype(float).copy())
    else:                       # single track, absolute gates only
        h.update(na=1, az_track_tol=0.0, cri_track_tol=0.0,
                 refl_el_min=0.0, refl_az_tol=0.0, refl_spread_min=0.0,
                 cri_tol_1=h["cri_tol"], classify=1,
                 freq_lo_na=np.zeros(1), freq_thr_na=np.array([h["freq_thr"]]))
    return h


def _scan_dcl_header(header_raw: bytes):
    """Find and decode the DCLH constant meta chunk embedded in the WAV header
    tail (src/filing.cxx wavHeaderInit, USE_DCL builds). Scans the header region
    for the DCLH magic; returns the decoded dict, or None if absent/invalid."""
    n = len(header_raw) // 4
    if n < 5:
        return None
    words = np.frombuffer(header_raw, dtype="<u4", count=n)
    hits  = np.flatnonzero(words == DCL_HEADER_MAGIC)
    for pos in hits:
        nwords = int(words[pos + 4]) if pos + 4 < n else 0
        if 0 < nwords <= n - pos:
            try:
                return _decode_dcl_header(words[pos:pos + nwords])
            except Exception:
                continue
    return None


def merge_det5_events(events: list):
    """Concatenate decoded DET5 detections into stacked arrays, or None if empty.
    Includes the per-event resume state (detector seed + EM state) so offline can
    reconstruct the DCL from any detection."""
    if not events:
        return None
    col   = lambda k, dt: np.array([e[k] for e in events], dtype=dt)
    return dict(
        dcl_method = col("dcl_method", np.int64),
        unix_s     = col("unix_s",    np.int64),
        micros     = col("micros",    np.int64),
        direct_us  = col("direct_us", np.int64),
        ipi_us     = col("ipi_us",    np.int64),
        t          = col("t",        np.float32),
        freq_bf    = col("freq_bf",  np.float32),
        level_bf   = col("level_bf", np.float32),
        dir        = np.stack([e["dir"] for e in events]),
        az         = col("az",  np.float32),
        el         = col("el",  np.float32),
        spread     = col("spread", np.float32),   # NaN on method-1 records
        # signed: CLS_NONE (-1) is a real label here, and uint8 would turn it
        # into 255 — a class index no DCL_NA can produce, silently
        cls        = col("cls", np.int16),
        # detector resume seed
        seed_xtail = np.stack([e["seed_xtail"] for e in events]),   # (N, nch, 2)
        seed_wtail = np.stack([e["seed_wtail"] for e in events]),
        seed_ptrk  = col("seed_ptrk", np.float32),
        seed_Lf    = col("seed_Lf",   np.float32),
        # EM classifier resume state
        em_cri_a=col("em_cri_a", np.float32), em_cri_b=col("em_cri_b", np.float32),
        em_log_ici_fast=col("em_log_ici_fast", np.float32),
        em_log_ici_slow=col("em_log_ici_slow", np.float32),
        em_log_dmax_fast=col("em_log_dmax_fast", np.float32),
        em_log_dmax_slow=col("em_log_dmax_slow", np.float32),
        em_dir_click_run=np.stack([e["em_dir_click_run"] for e in events]),
        em_dmax_seeded=col("em_dmax_seeded", np.uint8),
    )


# ── NAD block decoders ───────────────────────────────────────────────────────

def _decode_nad_block(raw: bytes, pos: int = 0) -> tuple:
    """
    Decode one NAD block starting at byte offset `pos` in `raw`.

    Returns (block_dict, nbytes_consumed). block_dict["raw"] is the
    n_records x 16 int16 array; the named fields are views/slices of it.
    """
    hdr = struct.unpack_from("<8I", raw, pos)
    if hdr[0] != NAD_MAGIC:
        raise ValueError(f"Bad NAD magic: 0x{hdr[0]:08X}")
    n_records = hdr[5] // NAD_REC_INT16
    data_off  = pos + NAD_HDR_BYTES
    samples   = np.frombuffer(raw, dtype="<i2", count=NAD_REC_INT16 * n_records,
                               offset=data_off).reshape(n_records, NAD_REC_INT16)
    block = dict(
        rtc_sec   = int(hdr[1]),
        micros    = int(hdr[2]),
        n_records = n_records,
        accel     = samples[:, 0:3],
        mag       = samples[:, 3:6],
        gyro      = samples[:, 6:9],
        acc_temp  = samples[:, 9],
        mag_temp  = samples[:, 10],
        mcu_temp  = samples[:, 11],
        raw       = samples,
    )
    return block, NAD_HDR_BYTES + n_records * NAD_REC_BYTES


def _try_read_nad_block(fh):
    """
    If the next NAD_HDR_BYTES at fh's current position start with NAD_MAGIC,
    consume and decode the whole block and return it; otherwise rewind fh
    back to where it started and return None.
    """
    pos0 = fh.tell()
    hdr_bytes = fh.read(NAD_HDR_BYTES)
    if len(hdr_bytes) < NAD_HDR_BYTES or \
       struct.unpack_from("<I", hdr_bytes, 0)[0] != NAD_MAGIC:
        fh.seek(pos0)
        return None
    n_records  = struct.unpack_from("<8I", hdr_bytes, 0)[5] // NAD_REC_INT16
    data_bytes = fh.read(n_records * NAD_REC_BYTES)
    if len(data_bytes) < n_records * NAD_REC_BYTES:
        fh.seek(pos0)
        return None
    block, _ = _decode_nad_block(hdr_bytes + data_bytes, 0)
    return block


def merge_nad_blocks(blocks: list):
    """Concatenate a list of decoded NAD blocks into single arrays, or None if empty."""
    if not blocks:
        return None
    return dict(
        rtc_sec  = np.array([b["rtc_sec"] for b in blocks], dtype=np.int64),
        micros   = np.array([b["micros"]  for b in blocks], dtype=np.int64),
        accel    = np.concatenate([b["accel"]    for b in blocks], axis=0),
        mag      = np.concatenate([b["mag"]      for b in blocks], axis=0),
        gyro     = np.concatenate([b["gyro"]     for b in blocks], axis=0),
        acc_temp = np.concatenate([b["acc_temp"] for b in blocks], axis=0),
        mag_temp = np.concatenate([b["mag_temp"] for b in blocks], axis=0),
        mcu_temp = np.concatenate([b["mcu_temp"] for b in blocks], axis=0),
    )


def read_nad_calib_stream(path) -> list:
    """
    Read a microPAM_calibrate raw NAD dump (NAD_CALIB_<epoch>.bin):
    a bare sequence of NAD blocks (see module docstring), no WAV header,
    with a possibly shorter final block. Returns a list of block dicts
    (see _decode_nad_block); pass to merge_nad_blocks() to concatenate.
    """
    raw = Path(path).read_bytes()
    pos, n = 0, len(raw)
    blocks = []
    while pos + NAD_HDR_BYTES <= n:
        if struct.unpack_from("<I", raw, pos)[0] != NAD_MAGIC:
            break
        block, consumed = _decode_nad_block(raw, pos)
        blocks.append(block)
        pos += consumed
    return blocks


def read_nad_calib_result(path) -> dict:
    """
    Read a microPAM_calibrate fitted-calibration file (NAD_CALIB.dat):
    uint32[2] header {magic "CAL1", version} followed by a packed
    CalibResult struct (microPAM_calibrate/src/calib.h). Returns a dict of
    the struct fields (arrays copied out of the mmap'd record).
    """
    raw = Path(path).read_bytes()
    magic, version = struct.unpack_from("<2I", raw, 0)
    if magic != NAD_CALIB_MAGIC:
        raise ValueError(f"Bad NAD_CALIB magic: 0x{magic:08X}")
    rec = np.frombuffer(raw, dtype=CALIB_RESULT_DTYPE, count=1, offset=8)[0]
    result = {name: np.array(rec[name]) for name in CALIB_RESULT_DTYPE.names}
    for scalar_key in ("acc_rms", "mag_rms", "acc_octants", "mag_octants",
                        "n_samples", "sufficient"):
        result[scalar_key] = result[scalar_key].item()
    result["sufficient"] = bool(result["sufficient"])
    result["version"]    = version
    return result


# ── main reader class ──────────────────────────────────────────────────────────

class MicroPAMFile:
    """
    Reader for a microPAM_V5 data file (.wav raw PCM or .bin compressed).
    NAD (non-acoustic data) blocks and, when the opt-in DCL overlay is enabled,
    DET5 detection records interleaved into the file by filing.cxx / dcl.cxx are
    transparently stripped out of the decoded data stream and collected into
    `self.nad_blocks` / `self.det_events` (see module docstring,
    Docs/NAD_Module.md and Docs/DCL_Processing_Chain.md).

    Parameters
    ----------
    path        : str or Path
    block_words : uint32 words per disk block. Default 0 auto-derives it
                  from the file's own IKEY info chunk (NBUF_PROC *
                  nblks_per_disk = NBUF_DISK, only valid for a cleanly
                  closed file - see Docs/NAD_Module.md section 6), falling
                  back to DISK_BLOCK_WORDS_DEFAULT if that is unavailable
                  (e.g. an abnormally-terminated file).

    A file that was never closed cleanly carries no IKEY chunk at all; what
    can be recovered from the data stream itself then is, and `info`
    ["recovered"] is set - see `_recover_info()`.
    """

    DISK_BLOCK_WORDS_DEFAULT = 16384

    MAX_HEADER_BYTES = 8192   # upper bound while probing the (512-multiple) header

    RECOVER_PROBE_WORDS = 32768   # 128 kB of data stream is plenty to find a record

    def __init__(self, path, block_words: int = 0):
        self.path         = Path(path)
        self._fh          = None
        raw_header        = self.path.read_bytes()[:self.MAX_HEADER_BYTES]
        self.info         = _parse_wav_header(raw_header)
        self._header_bytes = self.info["header_bytes"]   # audio starts here (512-aligned)
        if not self.info["ikey_ok"]:
            recover_stream_info(self.path, self.info, self.RECOVER_PROBE_WORDS)
        pm = self.info["proc_mode"]
        if pm not in (0, 1):
            raise ValueError(f"Unsupported PROC_MODE {pm} in IKEY "
                             f"(V5 firmware writes only 0 raw / 1 compressed)")
        auto_words = self.info["nbuf_proc"] * self.info["nblks_per_disk"]
        self._block_words = block_words or auto_words or self.DISK_BLOCK_WORDS_DEFAULT
        self.nad_blocks    = []   # populated by iter_blocks()
        self.det_events    = []   # DET5 DCL detections, populated by iter_blocks()
        # DCL meta chunk (constants) lives in the header tail on USE_DCL builds.
        self.dcl_header    = _scan_dcl_header(raw_header[:self._header_bytes])

    def __enter__(self):
        self._fh = open(self.path, "rb")
        self._fh.seek(self._header_bytes)
        return self

    def __exit__(self, *_):
        if self._fh:
            self._fh.close()
            self._fh = None

    def iter_blocks(self):
        """
        Iterate over decoded data blocks.

        Yields dicts with "proc_mode", "unix"/"micros" (per-record header unit
        time, empty for raw PCM), and "data" (n_frames x n_channels int32).

        NAD blocks and interleaved DET5 DCL detections are stripped out of the
        stream as they're encountered and appended to `self.nad_blocks` /
        `self.det_events` respectively, instead of being yielded here (see
        Docs/NAD_Module.md and Docs/DCL_Processing_Chain.md).
        """
        pm        = self.info["proc_mode"]
        nch       = self.info["n_channels"]
        nbuf_proc = self.info["nbuf_proc"]
        bw        = self._block_words

        own_fh = self._fh is None
        fh     = open(self.path, "rb") if own_fh else self._fh
        self.nad_blocks = []
        self.det_events = []
        # Records run across disk-block boundaries. The words of a record that
        # is incomplete at the end of one block are carried over and put in
        # front of the next block, NAD blocks having been stripped in between.
        carry = np.empty(0, dtype="<u4")
        try:
            # Audio starts at the 512-aligned end of the header. The DCLH meta
            # chunk (if any) lives inside the header and was already decoded in
            # __init__, so the data stream here is audio + interleaved NAD/DET5.
            fh.seek(self._header_bytes)
            while True:
                nad_block = _try_read_nad_block(fh)
                if nad_block is not None:
                    self.nad_blocks.append(nad_block)
                    continue

                raw = fh.read(bw * 4)
                if not raw:
                    break
                # A short read is the end of the file and is taken as it stands.
                # filing.cxx writes whole 512-byte sectors and zero-fills a
                # partly used disk buffer, so the trailing zeros of the last
                # block are written data; padding the read on top of them would
                # append samples the file does not contain.
                data_u32 = np.frombuffer(raw, dtype="<u4", count=len(raw) // 4)
                if carry.size:
                    data_u32 = np.concatenate([carry, data_u32])

                if pm == 0:
                    used = (len(data_u32) // nch) * nch
                    carry = data_u32[used:]
                    yield {"proc_mode": pm, "unix": [], "micros": [],
                           "data": _decode_raw_pcm(data_u32[:used], nch)}

                elif _is_compressed(pm):
                    # DET5 detections (opt-in DCL) share the mode-1 stream.
                    recs, evs, used = _decode_stream_block(data_u32, nch, nbuf_proc)
                    carry = data_u32[used:]
                    self.det_events.extend(evs)
                    if recs:
                        yield {"proc_mode": pm,
                               "unix":   [r[1] for r in recs],
                               "micros": [r[2] for r in recs],
                               "data":   np.concatenate([r[0] for r in recs], axis=0)}
        finally:
            if own_fh:
                fh.close()

    def read_all(self) -> dict:
        """
        Read the entire file and return concatenated arrays. Result carries:
          "data"   : (N, n_channels) int32 samples
          "unix"   : per-record unix seconds (empty for raw PCM)
          "micros" : per-record free-running micros (empty for raw PCM)
          "nad"    : merged NAD sensor arrays (merge_nad_blocks()), or None
          "events" : merged DET5 DCL detections (merge_det5_events()), or None
        """
        pm = self.info["proc_mode"]

        arrays, unix, micros = [], [], []
        for block in self.iter_blocks():
            arrays.append(block["data"])
            unix.extend(block["unix"])
            micros.extend(block["micros"])
        data = np.concatenate(arrays, axis=0) if arrays else \
               np.empty((0, self.info["n_channels"]), dtype=np.int32)
        return {"proc_mode": pm, "data": data, "unix": unix, "micros": micros,
                "info": self.info,
                "nad":        merge_nad_blocks(self.nad_blocks),
                "events":     merge_det5_events(self.det_events),
                "dcl_header": self.dcl_header}


def _sniff_kind(path) -> str:
    """Classify a file by its first bytes: 'wav' (MicroPAMFile), 'nad_calib_dat'
    (microPAM_calibrate CalibResult) or 'nad_calib_bin' (raw NAD dump)."""
    head = Path(path).read_bytes()[:4]
    if head == b"RIFF":
        return "wav"
    magic = struct.unpack("<I", head)[0] if len(head) == 4 else 0
    if magic == NAD_CALIB_MAGIC:
        return "nad_calib_dat"
    if magic == NAD_MAGIC:
        return "nad_calib_bin"
    raise ValueError(f"Unrecognized file: {path} (first 4 bytes {head!r})")


# ── command-line quick-check ───────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python micropam_reader.py <file> [block_words]")
        sys.exit(1)

    path = sys.argv[1]
    bw   = int(sys.argv[2]) if len(sys.argv) > 2 else 0

    kind = _sniff_kind(path)

    if kind == "nad_calib_dat":
        calib = read_nad_calib_result(path)
        print(f"File   : {path}  (microPAM_calibrate CalibResult, version {calib['version']})")
        print(f"n_samples={calib['n_samples']}  sufficient={calib['sufficient']}")
        print(f"acc_bias={calib['acc_bias']}  acc_scale={calib['acc_scale']}  acc_rms={calib['acc_rms']:.4f}")
        print(f"mag_bias={calib['mag_bias']}  mag_rms={calib['mag_rms']:.4f}")
        print(f"mag_scale_matrix=\n{calib['mag_scale_matrix']}")
        print(f"gyro_bias={calib['gyro_bias']}  gyro_scale={calib['gyro_scale']}  gyro_rms={calib['gyro_rms']}")
        print(f"acc_octants=0x{calib['acc_octants']:02X}  mag_octants=0x{calib['mag_octants']:02X}")
        sys.exit(0)

    if kind == "nad_calib_bin":
        blocks = read_nad_calib_stream(path)
        merged = merge_nad_blocks(blocks)
        print(f"File   : {path}  (microPAM_calibrate raw NAD dump)")
        print(f"Decoded: {len(blocks)} blocks, {sum(b['n_records'] for b in blocks)} records")
        if merged is not None:
            print(f"  accel shape={merged['accel'].shape}  gyro shape={merged['gyro'].shape}  mag shape={merged['mag'].shape}")
            print(f"  rtc_sec first={merged['rtc_sec'][0]}  last={merged['rtc_sec'][-1]}")
        sys.exit(0)

    f   = MicroPAMFile(path, block_words=bw)
    nfo = f.info
    print(f"File   : {path}")
    if nfo["recovered"]:
        print("WARNING: no IKEY chunk - the file was never closed cleanly "
              "(power loss or a reboot).\n"
              "         Mode and NBUF_PROC were recovered from the data "
              "stream; gain, sensitivity,\n"
              "         UID and duty cycle are unrecoverable.")
    print(f"UID    : {nfo['uid']}")
    mode_kind = "compressed" if _is_compressed(nfo['proc_mode']) else "raw"
    print(f"Mode   : {nfo['proc_mode']}  ({mode_kind})"
          f"{'  [recovered]' if nfo['recovered'] else ''}")
    print(f"fsamp  : {nfo['sample_rate']} Hz  nch={nfo['n_channels']}  "
          f"bits={nfo['bits_per_sample']}")
    print(f"SHIFT  : {nfo['shift']}  NBUF_PROC={nfo['nbuf_proc']}  NAVG={nfo['navg']}")
    print(f"Version: {nfo['version']}")
    print()

    result = f.read_all()

    data = result["data"]
    print(f"Decoded: shape={data.shape}  dtype={data.dtype}")
    if data.size:
        print(f"         min={data.min()}  max={data.max()}")
    if result["unix"]:
        u, m = result["unix"], result["micros"]
        print(f"         {len(u)} records  unix first={u[0]} last={u[-1]}  "
              f"micros first={m[0]} last={m[-1]}")

    if result.get("dcl_header") is not None:
        h = result["dcl_header"]
        print(f"DCLH   : method={h['method']}  nch={h['nch']}  fsamp={h['fsamp']}  "
              f"thr_db={h['thr_db']:.1f}  tau={h['dcl_tau']:.4g}  tau_bg={h['dcl_tau_bg']:.4g}")
        print(f"         AR a1={np.round(h['ar_a1'],4)}  a2={np.round(h['ar_a2'],4)}")
        if not h["classify"]:
            # detection-only build: no classifier was compiled, so the gate
            # slots are NaN rather than windows that governed anything
            print("         gates none - detection-only build (DCL_CLASSIFY=0)")
        else:
            print(f"         gates direct el {h['el_dir_lo']:+.1f}..{h['el_dir_hi']:+.1f} deg  "
                  f"reflection el {h['el_refl_lo']:+.1f}..{h['el_refl_hi']:+.1f} deg  "
                  f"CRI {h['cri_lo']*1e3:.1f}..{h['cri_hi']*1e3:.1f} ms "
                  f"(init {h['cri_init']*1e3:.1f})")

    if result["events"] is not None:
        ev = result["events"]
        n_ev = len(ev["t"])
        n_none  = int((ev["cls"] == CLS_NONE).sum())
        n_click = int((ev["cls"] == 1).sum())
        n_refl  = int((ev["cls"] == 2).sum())
        labels = (f"{n_none} unclassified (detection-only build)" if n_none
                  else f"{n_click} click / {n_refl} reflection labels")
        print(f"DET5   : {n_ev} DCL detections  (method {int(ev['dcl_method'][0])}; "
              f"{labels})")
        print(f"         freq_bf min={ev['freq_bf'].min():.1f} "
              f"max={ev['freq_bf'].max():.1f} Hz")
        print(f"         resume: seed_ptrk[0]={ev['seed_ptrk'][0]:.3g} "
              f"seed_Lf[0]={ev['seed_Lf'][0]:.3g}  em_cri_a[0]={ev['em_cri_a'][0]:.4g}")

    if result["nad"] is not None:
        nad = result["nad"]
        print(f"NAD    : {len(nad['rtc_sec'])} blocks stripped, {nad['accel'].shape[0]} records total")
        print(f"         rtc_sec first={nad['rtc_sec'][0]}  last={nad['rtc_sec'][-1]}")
