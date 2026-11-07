# flash_rp2350.ps1 - compile and upload to RP2350 Adafruit Feather HSTX
. "$PSScriptRoot\common.ps1"
if (-not (Invoke-Build rp2350)) { exit 1 }
Invoke-Upload rp2350
