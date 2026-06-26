# upload_teensy.ps1 - upload pre-built firmware to Teensy 4.1 (no recompile)
# Reads the .hex from build\teensy\ and flashes it via arduino-cli + Teensy Loader.
# The Teensy must be connected; press the PROGRAM MODE button if prompted.

$Root   = Split-Path $PSScriptRoot -Parent
$CLI    = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$DATA   = "$env:LOCALAPPDATA\Arduino15"
$FQBN   = "teensy:avr:teensy41"
$OUTDIR = "$Root\build\teensy"
$HEX    = "$OUTDIR\microPAM_V4.ino.hex"

if (-not (Test-Path $HEX)) {
    Write-Host "No pre-built firmware found at $HEX" -ForegroundColor Red
    Write-Host "Run .\scripts\build.ps1 teensy  (or flash_teensy.ps1) first." -ForegroundColor Yellow
    exit 1
}

# --- detect port ---
$board = & $CLI board list --config-dir $DATA 2>&1 |
         Where-Object { $_ -match "teensy:avr:teensy41" } |
         Select-Object -First 1
if (-not $board) {
    Write-Host "Teensy 4.1 not detected. Connect the board and try again." -ForegroundColor Red
    exit 1
}
$port = ($board -split '\s+')[0]
Write-Host "Uploading pre-built firmware to $port ..." -ForegroundColor Cyan

# --- upload ---
& $CLI upload --fqbn $FQBN --config-dir $DATA --port $port --input-dir $OUTDIR $Root 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host "Upload failed." -ForegroundColor Red; exit 1 }
Write-Host "Upload OK" -ForegroundColor Green
