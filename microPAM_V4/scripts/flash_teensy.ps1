# flash_teensy.ps1 — compile and upload to Teensy 4.1
# The Teensy must be connected (Teensy Loader detects it automatically).

$Root   = Split-Path $PSScriptRoot -Parent
$CLI    = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$DATA   = "$env:LOCALAPPDATA\Arduino15"
$FQBN   = "teensy:avr:teensy41"
$OUTDIR = "$Root\build\teensy"

# --- compile ---
Write-Host "=== Compiling for Teensy 4.1 ===" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $OUTDIR | Out-Null
& $CLI compile --fqbn $FQBN --config-dir $DATA --output-dir $OUTDIR $Root 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host "Compile failed." -ForegroundColor Red; exit 1 }
Remove-Item "$OUTDIR\*.eep" -ErrorAction SilentlyContinue
Write-Host "Compile OK" -ForegroundColor Green

# --- detect port ---
$board = & $CLI board list --config-dir $DATA 2>&1 |
         Where-Object { $_ -match "teensy:avr:teensy41" } |
         Select-Object -First 1
if (-not $board) {
    Write-Host "Teensy 4.1 not detected. Connect the board and try again." -ForegroundColor Red
    exit 1
}
$port = ($board -split '\s+')[0]
Write-Host "Uploading to $port ..." -ForegroundColor Cyan

# --- upload ---
& $CLI upload --fqbn $FQBN --config-dir $DATA --port $port --input-dir $OUTDIR $Root 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host "Upload failed." -ForegroundColor Red; exit 1 }
Write-Host "Upload OK" -ForegroundColor Green
