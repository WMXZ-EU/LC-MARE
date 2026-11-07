# flash_rp2040.ps1 - compile and upload to RP2040 Adafruit Feather Adalogger
. "$PSScriptRoot\common.ps1"
if (-not (Invoke-Build rp2040)) { exit 1 }
Invoke-Upload rp2040
