# upload_rp2040.ps1 - upload pre-built firmware to RP2040 Adalogger (no recompile)
# Reads the .uf2 from build\rp2040\ and flashes it.
# Two upload paths are tried in order:
#   1. BOOTSEL mass-storage drive (RPI-RP2) - copies .uf2 directly.
#   2. Serial port - arduino-cli resets the board into bootloader.

$Root   = Split-Path $PSScriptRoot -Parent
$CLI    = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$DATA   = "$env:LOCALAPPDATA\Arduino15"
$FQBN   = "rp2040:rp2040:adafruit_feather_adalogger"
$OUTDIR = "$Root\build\rp2040"
$UF2    = "$OUTDIR\microPAM_V4.ino.uf2"

if (-not (Test-Path $UF2)) {
    Write-Host "No pre-built firmware found at $UF2" -ForegroundColor Red
    Write-Host "Run .\scripts\build.ps1 rp2040  (or flash_rp2040.ps1) first." -ForegroundColor Yellow
    exit 1
}

# --- detect BOOTSEL drive (RPI-RP2) ---
$drive = Get-PSDrive -PSProvider FileSystem |
         Where-Object { (Get-Volume -DriveLetter $_.Name -ErrorAction SilentlyContinue).FileSystemLabel -eq "RPI-RP2" } |
         Select-Object -First 1

if ($drive) {
    $dest = "$($drive.Name):\microPAM_V4.ino.uf2"
    Write-Host "BOOTSEL drive found at $($drive.Name):\ - copying UF2 ..." -ForegroundColor Cyan
    Copy-Item $UF2 $dest
    Write-Host "Upload OK" -ForegroundColor Green
} else {
    # --- detect serial port and upload ---
    $board = & $CLI board list --config-dir $DATA 2>&1 |
             Where-Object { $_ -match "adafruit_feather_adalogger|RP2040" } |
             Select-Object -First 1
    if (-not $board) {
        Write-Host "RP2040 Adalogger not detected. Connect the board (or hold BOOTSEL while plugging in) and try again." -ForegroundColor Red
        exit 1
    }
    $port = ($board -split '\s+')[0]
    Write-Host "Uploading pre-built firmware via $port ..." -ForegroundColor Cyan
    & $CLI upload --fqbn $FQBN --config-dir $DATA --port $port --input-dir $OUTDIR $Root 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Host "Upload failed." -ForegroundColor Red; exit 1 }
    Write-Host "Upload OK" -ForegroundColor Green
}
