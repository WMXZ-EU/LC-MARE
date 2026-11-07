# workbench.ps1 - start the microPAM_V3 workbench (Python\workbench.py)
# Creates Python\.venv (scripts\setup.ps1) on first use.
# Usage:
#   .\scripts\workbench.ps1          # Firmware tab in user mode (upload a .uf2 file)
#   .\scripts\workbench.ps1 -Dev     # Firmware tab in developer mode (build from source)
# From Explorer, double-click workbench.cmd or workbench_dev.cmd in the project folder.

param([switch]$Dev)

$Root = Split-Path $PSScriptRoot -Parent
$Py   = "$Root\Python\.venv\Scripts\python.exe"

if (-not (Test-Path $Py)) {
    & "$PSScriptRoot\setup.ps1"
    if ($LASTEXITCODE -ne 0) { exit 1 }
}
$wbArgs = @("$Root\Python\workbench.py")
if ($Dev) { $wbArgs += "--dev" }
& "$Root\Python\.venv\Scripts\pythonw.exe" @wbArgs
