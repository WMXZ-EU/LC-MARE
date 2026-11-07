# upload_uf2.ps1 - upload a ready-built .uf2 firmware file (no compiler needed)
# Usage:
#   .\scripts\upload_uf2.ps1 <file.uf2>               # board already in BOOTSEL mode
#   .\scripts\upload_uf2.ps1 <file.uf2> -Port COM41   # running board, reset via serial
# The file's chip (RP2040 / RP2350) must match the board; see Python\uf2_upload.py.

param(
    [Parameter(Mandatory = $true)][string]$File,
    [string]$Port = ""
)

$Root = Split-Path $PSScriptRoot -Parent
$Py   = "$Root\Python\.venv\Scripts\python.exe"
if (-not (Test-Path $Py)) { $Py = "python" }

$pyArgs = @("$Root\Python\uf2_upload.py", $File)
if ($Port) { $pyArgs += @("--port", $Port) }
& $Py @pyArgs
exit $LASTEXITCODE
