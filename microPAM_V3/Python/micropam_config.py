"""
micropam_config.py — the microPAM `config.txt` format (V5 layout, used by V3)
=============================================================================

Copied from microPAM_V5. microPAM_V3 (firmware 3.3.0 and later) writes and
reads the same file, so this module is shared by `workbench.py` and
`micropam_browser.py`. Standard library only.

V3 difference: the V3 serial menu also exposes s/c/e (ISRC/ICMS/ISBJ) as
`?s/!s`, `?c/!c`, `?e/!e`, and `w` writes /config.txt to the card at once, so
every key of the file can be set on a running board (except `m`, which is
compile-time). The table below describes the V5 menu; for V3 read the s/c/e
row as "same key in both".

The firmware reads `/config.txt` once at boot (`configShow()` ->
`loadConfigfromFile()`, src/filing.cxx) and writes it back on `SD_stop()`
(`storeConfigToFile()`). Those two functions are the format specification;
`parse_config_text()` and `format_config_text()` below mirror them, quirks
included.

Format
------
`#` comment lines, then parameter lines `!<key> <value><sep># comment`.
`format_config_text()` reproduces `storeConfigToFile()`'s exact layout — key
order and the two-space vs tab separator each line uses — so a file written
here is byte-identical to one the board writes.

Parser behaviour, all of it deliberate:
  * The scan for `!` runs over the whole buffer, not line by line, so a `!`
    inside a `#` comment is still a live entry — prefixing a line with `#`
    does NOT disable it.
  * A value runs from two characters after the key to the first `#` or `;`
    (bounded at 80 chars), and is then read with sscanf `%lu` / `%39s`, so
    only the first whitespace-delimited token survives. String values
    therefore cannot contain spaces and are truncated at 39 characters.
  * Duplicate keys are applied in file order — the last one wins.

Key namespace
-------------
**These keys are not the serial menu's.** They differ in three places, and
using the wrong table silently writes the wrong parameter:

  key  config.txt              serial menu
  p    IPRD (project)          -- not a key --
  k    -- not a key --         IPRD
  m    PROC_MODE (see below)   PROC_MODE (query only)
  f    fsamp in kHz            fsamp in Hz
  s/c/e  ISRC/ICMS/ISBJ        not exposed at all

`m` (project mode) is reported, not applied: PROC_MODE is a compile-time
`#define` (src/process.cxx selects the mode with `#if`), so the firmware
writes the value it was built with and ignores whatever the file says —
`loadConfigfromFile()`'s `case 'm'` is deliberately empty. Editing it here
records intent; changing the mode still means rebuilding with a different
`config.h`. It occupies the key the serial menu also uses for PROC_MODE, so
the two agree on `m` even though the rest of the namespace does not.

Callers holding fsamp in Hz (both GUIs do) must convert on the file boundary.
"""

from pathlib import Path

CONFIG_FILENAME = "config.txt"

# (key, separator the firmware puts before the comment, comment text) in
# storeConfigToFile() order.
CONFIG_LINES = [
    ("a", "  ", "file size (sec)"),
    ("o", "  ", "on time (min)"),
    ("r", "  ", "repetition interval (min)"),
    ("f", "\t", "sampling frequency (kHz)"),
    ("g", "\t", "analog gain (dB)"),
    ("m", "\t", "project mode (0: raw; 1:compressed)"),
    ("s", "\t", "(ISRC) source with sensitivity"),
    ("c", "\t", "(ICMS) commissioning organization"),
    ("n", "\t", "(IART) name of operator (creator)"),
    ("p", "\t", "(IPRD) project"),
    ("e", "\t", "(ISBJ) area"),
    ("l", "\t", "(INAM) location id"),
    ("1", "\t", "h_rec[0]"),
    ("2", "\t", "h_rec[1]"),
    ("3", "\t", "h_rec[2]"),
    ("4", "\t", "h_rec[3]"),
    ("x", "  ", "start time yyyy-mm-dd_hh:mm:ss"),
]

CONFIG_KEYS = [key for key, _sep, _comment in CONFIG_LINES]

# Values used when a file predates a key. `m` was added after the first cards
# were written, and is report-only anyway (the firmware rewrites it with its
# compiled-in PROC_MODE on the next SD_stop), so defaulting it beats making
# every old card an error.
CONFIG_DEFAULTS = {"m": "1"}

# Keys written with %s and read back with %39s — no spaces, max 39 chars.
CONFIG_STRING_KEYS = set("scnpelx")

CONFIG_HEADER = ("# configuration file\n"
                 "# should end with '#' or ';' comment may follow\n"
                 "#\n")


def parse_config_text(text: str) -> dict:
    """Parse config.txt exactly as loadConfigfromFile() does.

    Returns {key: token}. Unknown keys are kept (callers ignore them), and
    keys whose value slice holds no token are omitted — which is what the
    firmware effectively does, since sscanf leaves the variable untouched.
    """
    out = {}
    n = len(text)
    # Collect every '!' first and decode each independently, as the firmware
    # does, so an entry whose value slice runs past a later '!' cannot swallow
    # it.
    for i in (j for j, ch in enumerate(text) if ch == "!"):
        i1 = i + 1
        if i1 >= n:
            break
        key = text[i1]
        end = i1 + 1
        limit = min(i1 + 80, n)
        while end < limit and text[end] not in "#;":
            end += 1
        tokens = text[i1 + 2 : end].split()
        if tokens:
            out[key] = tokens[0]
    return out


def format_config_text(values: dict) -> str:
    """Render config.txt in storeConfigToFile()'s layout. `values` must supply
    every key in CONFIG_KEYS, already validated and in the file's own units
    (i.e. fsamp in kHz)."""
    parts = [CONFIG_HEADER]
    for key, sep, comment in CONFIG_LINES:
        parts.append(f"!{key} {values[key]}{sep}# {comment}\n")
    return "".join(parts)


def validate_config_value(key: str, val: str):
    """Check one file-domain value against what the firmware's parser accepts.

    Returns an error string, or None if the value survives a round trip. Note
    `key` 'f' is checked as kHz — callers whose field is in Hz must convert
    before calling.
    """
    if not val:
        return f"!{key}: empty"
    if key in CONFIG_STRING_KEYS:
        if len(val.split()) > 1:
            return (f"!{key}: contains a space — the firmware would keep only "
                    f"{val.split()[0]!r}")
        if len(val) > 39:
            return f"!{key}: {len(val)} chars, max 39"
        return None
    try:
        int(val)
    except ValueError:
        return f"!{key}: {val!r} is not an integer"
    return None


def read_config_file(path) -> dict:
    """Parse a config.txt from disk. Raises OSError if it cannot be read."""
    return parse_config_text(
        Path(path).read_text(encoding="utf-8", errors="replace"))


def write_config_file(path, values: dict):
    """Write config.txt, LF-terminated like the firmware's own output."""
    with open(path, "w", encoding="ascii", newline="\n") as fh:
        fh.write(format_config_text(values))
