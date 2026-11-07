# build.ps1 - compile microPAM_V3 (no board needed)
# Usage:
#   .\scripts\build.ps1            # all targets
#   .\scripts\build.ps1 teensy     # Teensy 4.1
#   .\scripts\build.ps1 rp2040     # RP2040 Adafruit Feather Adalogger
#   .\scripts\build.ps1 rp2350     # RP2350 Adafruit Feather HSTX (8 MB PSRAM)

param([string]$Target = "all")
. "$PSScriptRoot\common.ps1"

$targets = if ($Target -eq "all") { @($BOARDS.Keys) } else { @($Target) }
$failed = @()
foreach ($t in $targets) {
    if (-not (Invoke-Build $t)) { $failed += $t }
}

if ($failed.Count -gt 0) {
    Write-Host "`nFailed: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
Write-Host "`nAll builds passed." -ForegroundColor Green
