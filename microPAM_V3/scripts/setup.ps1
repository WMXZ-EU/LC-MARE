# setup.ps1 - create the Python environment (Python\.venv) for the workbench and scripts
# and report whether Arduino is installed for building firmware from source.
# Usage:
#   .\scripts\setup.ps1            # create Python\.venv if it does not exist
#   .\scripts\setup.ps1 -Force     # delete and recreate it
# From Explorer, double-click setup.cmd. scripts\workbench.ps1 runs this on first start.
#
# Needs Python 3.10 or later with tkinter (python.org installer, default options). The
# pinned versions of Python\requirements.txt are tried first; if pip cannot install them
# (no wheels for this Python version), the current numpy, matplotlib and pyserial are
# installed instead.

param([switch]$Force)

$Root = Split-Path $PSScriptRoot -Parent
$Venv = "$Root\Python\.venv"
$Py   = "$Venv\Scripts\python.exe"
$Check = "import sys, tkinter; sys.exit(0 if sys.version_info >= (3, 10) else 1)"

# a Python interpreter that runs, is 3.10+ and has tkinter; the py launcher first, as
# 'python' may be the Microsoft Store stub, which only opens the Store
function Find-Python {
    foreach ($c in @(@("py", "-3"), @("python"), @("python3"))) {
        if (-not (Get-Command $c[0] -ErrorAction SilentlyContinue)) { continue }
        $exe  = $c[0]
        $pre  = @($c | Select-Object -Skip 1)
        & $exe @pre -c $Check 2>$null
        if ($LASTEXITCODE -eq 0) { return , $c }
    }
    return $null
}

if ($Force -and (Test-Path $Venv)) {
    Write-Host "Removing Python\.venv ..." -ForegroundColor Cyan
    Remove-Item -Recurse -Force $Venv
}

if (Test-Path $Py) {
    Write-Host "Python\.venv exists (setup.ps1 -Force recreates it)." -ForegroundColor Green
} else {
    $python = Find-Python
    if (-not $python) {
        Write-Host "No Python 3.10+ with tkinter found. Install Python from https://www.python.org" -ForegroundColor Red
        Write-Host "(default options, which include tkinter and the 'py' launcher) and run setup again." -ForegroundColor Red
        exit 1
    }
    $exe = $python[0]
    $pre = @($python | Select-Object -Skip 1)
    $ver = & $exe @pre -c "import sys; print(sys.version.split()[0])"
    Write-Host "Creating Python\.venv with Python $ver ..." -ForegroundColor Cyan
    & $exe @pre -m venv $Venv
    if (-not (Test-Path $Py)) { Write-Host "Creating the venv failed." -ForegroundColor Red; exit 1 }

    & $Py -m pip install --disable-pip-version-check -r "$Root\Python\requirements.txt"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "The pinned versions do not install with Python $ver; installing current versions ..." -ForegroundColor Yellow
        & $Py -m pip install --disable-pip-version-check numpy matplotlib pyserial
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "pip install failed (no internet connection?)." -ForegroundColor Red
        Remove-Item -Recurse -Force $Venv
        exit 1
    }
    & $Py -c "import numpy, matplotlib, serial, tkinter"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "The new environment cannot import its packages." -ForegroundColor Red
        if ($Venv.Length -gt 100) {
            # DLL paths inside the venv then pass the 260-character limit of Windows
            Write-Host "The folder path is long ($($Root.Length) characters); move the folder to a short" -ForegroundColor Red
            Write-Host "path such as C:\microPAM_V3 and run setup again." -ForegroundColor Red
        }
        Remove-Item -Recurse -Force $Venv
        exit 1
    }
    Write-Host "Python\.venv ready." -ForegroundColor Green
}

# Arduino: only needed to build firmware from source (workbench developer mode)
. "$PSScriptRoot\common.ps1"
if (-not $CLI) {
    Write-Host "Arduino not found: firmware can be uploaded from .uf2 files, but not built from" -ForegroundColor Yellow
    Write-Host "source. To build, install Arduino IDE 2 and its board package 'Raspberry Pi" -ForegroundColor Yellow
    Write-Host "Pico/RP2040/RP2350' by Earle F. Philhower (see README.txt)." -ForegroundColor Yellow
} elseif (-not (Test-Path "$DATA\packages\rp2040\hardware\rp2040")) {
    Write-Host "Arduino found ($CLI), but the board package 'Raspberry Pi Pico/RP2040/RP2350'" -ForegroundColor Yellow
    Write-Host "by Earle F. Philhower is not installed; building from source stays disabled." -ForegroundColor Yellow
} else {
    Write-Host "Arduino found: $CLI" -ForegroundColor Green
}
exit 0
