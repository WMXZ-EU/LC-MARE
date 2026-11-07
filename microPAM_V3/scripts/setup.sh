#!/usr/bin/env bash
# setup.sh - create the Python environment (Python/.venv) for the workbench and scripts on
# macOS, and report whether Arduino is installed for building firmware from source.
# The counterpart of setup.ps1 on Windows.
# Usage (in Terminal, from the project folder):
#   bash scripts/setup.sh            # create Python/.venv if it does not exist
#   bash scripts/setup.sh --force    # delete and recreate it
#
# Needs Python 3.10 or later with tkinter on Tk 8.6 or later. The python.org installer has
# both; Homebrew Python needs the matching python-tk formula (e.g. brew install
# python-tk@3.13). Apple's /usr/bin/python3 (Xcode command line tools) is 3.9 and is passed
# over. The pinned versions of Python/requirements.txt are tried first; if pip cannot
# install them (no wheels for this Python version), the current numpy, matplotlib and
# pyserial are installed instead.
# Written for the bash 3.2 that macOS ships.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENV="$ROOT/Python/.venv"
PY="$VENV/bin/python"
CHECK='import sys, tkinter; sys.exit(0 if sys.version_info >= (3, 10) and tkinter.TkVersion >= 8.6 else 1)'

FORCE=0
for a in "$@"; do
    case "$a" in
        -f|--force|-Force) FORCE=1 ;;
        *) echo "Unknown argument '$a'. Usage: bash scripts/setup.sh [--force]"; exit 1 ;;
    esac
done

if [ -t 1 ]; then
    RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; CYAN=$'\033[36m'; OFF=$'\033[0m'
else
    RED=; GREEN=; YELLOW=; CYAN=; OFF=
fi
say() { printf '%s%s%s\n' "$1" "$2" "$OFF"; }

# a Python interpreter that runs, is 3.10+ and has tkinter on Tk 8.6+: the versioned names
# on the PATH (newest first), then python3, then python.org installs not on the PATH
find_python() {
    local c
    for c in python3.14 python3.13 python3.12 python3.11 python3.10 python3 \
             $(ls -d /Library/Frameworks/Python.framework/Versions/3.*/bin/python3 2>/dev/null |
               sort -t. -k2 -n -r); do
        c="$(command -v "$c" 2>/dev/null)" || continue
        if "$c" -c "$CHECK" 2>/dev/null; then echo "$c"; return 0; fi
    done
    return 1
}

if [ "$FORCE" = 1 ] && [ -d "$VENV" ]; then
    say "$CYAN" "Removing Python/.venv ..."
    rm -rf "$VENV"
fi

if [ -x "$PY" ]; then
    say "$GREEN" "Python/.venv exists (setup.sh --force recreates it)."
else
    if ! PYTHON="$(find_python)"; then
        say "$RED" "No Python 3.10+ with tkinter (Tk 8.6+) found. Install Python from"
        say "$RED" "https://www.python.org (the macOS installer includes tkinter), or with Homebrew"
        say "$RED" "both python and python-tk, and run setup again."
        exit 1
    fi
    VER="$("$PYTHON" -c "import sys; print(sys.version.split()[0])")"
    say "$CYAN" "Creating Python/.venv with Python $VER ($PYTHON) ..."
    "$PYTHON" -m venv "$VENV"
    if [ ! -x "$PY" ]; then say "$RED" "Creating the venv failed."; exit 1; fi

    if ! "$PY" -m pip install --disable-pip-version-check -r "$ROOT/Python/requirements.txt"; then
        say "$YELLOW" "The pinned versions do not install with Python $VER; installing current versions ..."
        if ! "$PY" -m pip install --disable-pip-version-check numpy matplotlib pyserial; then
            say "$RED" "pip install failed (no internet connection?)."
            rm -rf "$VENV"
            exit 1
        fi
    fi
    if ! "$PY" -c "import numpy, matplotlib, serial, tkinter"; then
        say "$RED" "The new environment cannot import its packages."
        rm -rf "$VENV"
        exit 1
    fi
    say "$GREEN" "Python/.venv ready."
fi

# Arduino: only needed to build firmware from source. arduino-cli is searched for as in
# common.ps1 (Find-ArduinoCli): $ARDUINO_CLI, the one bundled with Arduino IDE 2, or
# arduino-cli on the PATH (e.g. from Homebrew).
DATA="$HOME/Library/Arduino15"
IDE="Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
CLI=
for c in "${ARDUINO_CLI:-}" "/Applications/$IDE" "$HOME/Applications/$IDE" \
         "$(command -v arduino-cli 2>/dev/null)"; do
    if [ -n "$c" ] && [ -f "$c" ] && [ -x "$c" ]; then CLI="$c"; break; fi
done
if [ -z "$CLI" ]; then
    say "$YELLOW" "Arduino not found: firmware can be uploaded from .uf2 files, but not built from"
    say "$YELLOW" "source. To build, install Arduino IDE 2 and its board package 'Raspberry Pi"
    say "$YELLOW" "Pico/RP2040/RP2350' by Earle F. Philhower (see README.txt)."
elif [ ! -d "$DATA/packages/rp2040/hardware/rp2040" ]; then
    say "$YELLOW" "Arduino found ($CLI), but the board package 'Raspberry Pi Pico/RP2040/RP2350'"
    say "$YELLOW" "by Earle F. Philhower is not installed in $DATA."
else
    say "$GREEN" "Arduino found: $CLI"
fi
exit 0
