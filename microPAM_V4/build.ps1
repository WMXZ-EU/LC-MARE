# build.ps1 — compile microPAM_V4 for one or all targets
# Usage:
#   .\build.ps1            # all three targets
#   .\build.ps1 teensy     # Teensy 4.1
#   .\build.ps1 rp2040     # RP2040 Adafruit Feather Adalogger
#   .\build.ps1 rp2350     # RP2350 Adafruit Feather HSTX (8 MB PSRAM)

param(
    [string]$Target = "all"
)

$CLI  = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$DATA = "$env:LOCALAPPDATA\Arduino15"
$SKETCH = "$PSScriptRoot"

$BOARDS = @{
    teensy = "teensy:avr:teensy41"
    rp2040 = "rp2040:rp2040:adafruit_feather_adalogger"
    rp2350 = "rp2040:rp2040:adafruit_feather_rp2350_hstx:psram=8mb"
}

$targets = if ($Target -eq "all") { $BOARDS.Keys } else { @($Target) }

$failed = @()
foreach ($t in $targets) {
    if (-not $BOARDS.ContainsKey($t)) {
        Write-Host "Unknown target '$t'. Valid: teensy, rp2040, rp2350, all" -ForegroundColor Red
        exit 1
    }
    $outDir = "$PSScriptRoot\build\$t"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    Write-Host "`n=== $t ===" -ForegroundColor Cyan
    & $CLI compile --fqbn $BOARDS[$t] --config-dir $DATA --output-dir $outDir $SKETCH 2>&1
    if ($LASTEXITCODE -eq 0) {
        if ($t -eq "teensy") { Remove-Item "$outDir\*.eep" -ErrorAction SilentlyContinue }
        Write-Host "OK $t  ->  build\$t" -ForegroundColor Green
    } else {
        Write-Host "FAILED $t" -ForegroundColor Red
        $failed += $t
    }
}

if ($failed.Count -gt 0) {
    Write-Host "`nFailed: $($failed -join ', ')" -ForegroundColor Red
    exit 1
} else {
    Write-Host "`nAll builds passed." -ForegroundColor Green
}
