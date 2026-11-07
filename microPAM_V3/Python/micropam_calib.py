"""
micropam_calib.py — acoustic calibration of microPAM recordings from the ISRC chunk
===================================================================================

The firmware writes the source string `SRC_str` (config.h, `!s` in config.txt) into
the ISRC chunk of every file. When it has the form

    <name>-<C0>          e.g.  LC17-38.7

the number C0 is the calibration of the source: the level in dB re 1 µPa that
corresponds to one count of the 24-bit ADC word (SHIFT 8) at 0 dB analog gain. It
describes hydrophone, preamplifier and converter together and does not depend on
the settings of a recording, which are taken from IKEY instead. One count of the
samples of a file, as the readers return them, then corresponds to

    C = C0 - again + 20 log10(2^(SHIFT - 8))   dB re 1 µPa,

where again is the analog gain in dB and SHIFT the right shift applied before
compression (12 in PROC_MODE 1, 0 in PROC_MODE 0 and in files from bin2wav.py).
A sample I in counts is a sound pressure p = I * 10^(C/20) µPa, and a spectral
level in dB re 1 count²/Hz becomes one in dB re 1 µPa²/Hz by adding C.

LC17 (SQ17-00 hydrophone on the LC-Mare front end): the resistor measurement on
unit 1B843C2E (tests/characterization_1B843C2E.md) gave a system gain of 158.2 dB
re 1 count/V at 20 dB gain and SHIFT 12. With M = -201 dB re 1 V/µPa, one count is
then 42.8 dB re 1 µPa, and C0 = 42.8 - 20 - 24.08 = 38.7.

A string without a numeric suffix (e.g. "LC17"), or with a resistor value as in the
characterization sessions, gives no calibration; a suffix is accepted only within
CAL_RANGE, so that a hydrophone sensitivity in the old form "AS1-208" is not taken
for a count level.

Mirrored in pamlib.io.micropam_calib (Passive Acoustics); keep the two in step.
"""

from __future__ import annotations

import math

import numpy as np

ADC_SHIFT = 8                   # the 24-bit ADC word sits in the top bits of 32
CAL_RANGE = (-20.0, 100.0)      # plausible C0, dB re 1 µPa per 24-bit count
SHIFT_MODE1 = 12                # SHIFT of every compressing firmware so far


def parse_isrc(isrc: str) -> tuple[str, float | None]:
    """Split an ISRC string "<name>-<C0>" into (name, C0). C0 is None when the
    suffix is missing, not a number, or outside CAL_RANGE."""
    isrc = (isrc or "").strip()
    name, sep, tail = isrc.rpartition("-")
    if not sep or not name:
        return isrc, None
    try:
        c0 = float(tail)
    except ValueError:
        return isrc, None
    if not (CAL_RANGE[0] <= c0 <= CAL_RANGE[1]) or not math.isfinite(c0):
        return isrc, None
    return name, c0


def count_level_db(info: dict) -> float | None:
    """Level in dB re 1 µPa of one count of the file's samples, or None without a
    calibration in ISRC. `info` is a parsed header (reader `_parse_wav_header`,
    `MicroPAMFile.info`, or the browser's `_safe_parse`)."""
    _, c0 = parse_isrc(info.get("chunks", {}).get("ISRC", ""))
    if c0 is None:
        return None
    shift = info.get("shift") or 0
    if shift == 0 and info.get("proc_mode") == 1:
        shift = SHIFT_MODE1                 # no IKEY (file never closed), as bin2wav.py
    again = info.get("again") or 0
    return c0 - again + 20 * math.log10(2.0) * (shift - ADC_SHIFT)


def count_to_upa(info: dict) -> float | None:
    """Sound pressure in µPa of one count of the file's samples, or None."""
    c = count_level_db(info)
    return None if c is None else 10 ** (c / 20)


def to_upa(data, info: dict) -> np.ndarray:
    """Samples in counts -> sound pressure in µPa (float64). Raises ValueError when
    the file carries no calibration."""
    k = count_to_upa(info)
    if k is None:
        raise ValueError(f"no calibration in ISRC {info.get('chunks', {}).get('ISRC')!r}"
                         " (expected <name>-<C0>, e.g. LC17-38.7)")
    return np.asarray(data, dtype=np.float64) * k
