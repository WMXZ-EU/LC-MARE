# common.ps1 - shared settings and functions, dot-sourced by the other scripts

$Root   = Split-Path $PSScriptRoot -Parent
$Sketch = (Get-ChildItem "$Root\*.ino" | Select-Object -First 1).BaseName   # microPAM_V3
$DATA   = "$env:LOCALAPPDATA\Arduino15"

# arduino-cli: $env:ARDUINO_CLI (set by the workbench), the one bundled with Arduino IDE 2
# (per-user or all-users install), or arduino-cli on the PATH. Python\workbench.py
# (find_arduino) searches the same places.
function Find-ArduinoCli {
    $ide = "resources\app\lib\backend\resources\arduino-cli.exe"
    $candidates = @($env:ARDUINO_CLI,
                    "$env:LOCALAPPDATA\Programs\Arduino IDE\$ide",
                    "$env:ProgramFiles\Arduino IDE\$ide")
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c -PathType Leaf)) { return $c }
    }
    $cmd = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}
$CLI = Find-ArduinoCli

# the scripts that compile or use arduino-cli call this first
# (with a target, the board package of that target is checked, else the RP2040 one)
function Assert-Arduino([string]$t = "") {
    if (-not $CLI) {
        Write-Host "arduino-cli not found. Install Arduino IDE 2 (or arduino-cli) and the board" -ForegroundColor Red
        Write-Host "package 'Raspberry Pi Pico/RP2040/RP2350' by Earle F. Philhower; see README.txt." -ForegroundColor Red
        exit 1
    }
    if ($t -and $BOARDS.Contains($t) -and $BOARDS[$t].Core -eq "teensy") {
        if (-not (Test-Path "$DATA\packages\teensy\hardware\avr")) {
            Write-Host "arduino-cli found ($CLI), but the Teensy board package is not installed in" -ForegroundColor Red
            Write-Host "$DATA. Install 'Teensy' by Paul Stoffregen (Teensyduino)." -ForegroundColor Red
            exit 1
        }
        return
    }
    if (-not (Test-Path "$DATA\packages\rp2040\hardware\rp2040")) {
        Write-Host "arduino-cli found ($CLI), but the RP2040 board package is not installed in" -ForegroundColor Red
        Write-Host "$DATA. Install 'Raspberry Pi Pico/RP2040/RP2350' by Earle F. Philhower." -ForegroundColor Red
        exit 1
    }
}

# target -> board FQBN, BOOTSEL drive label, pattern for 'arduino-cli board list',
# board package, firmware file extension
$BOARDS = [ordered]@{
    rp2040 = @{ Fqbn  = "rp2040:rp2040:adafruit_feather_adalogger"
                Label = "RPI-RP2"
                Match = "adafruit_feather_adalogger|RP2040"
                Name  = "RP2040 Feather Adalogger"
                Core  = "rp2040"
                Ext   = "uf2" }
    rp2350 = @{ Fqbn  = "rp2040:rp2040:adafruit_feather_rp2350_hstx:psram=8mb"
                Label = "RP2350"
                Match = "adafruit_feather_rp2350_hstx|RP2350"
                Name  = "RP2350 Feather HSTX"
                Core  = "rp2040"
                Ext   = "uf2" }
    teensy = @{ Fqbn  = "teensy:avr:teensy41:speed=24"   # CPU 24 MHz (power)
                Label = ""
                Match = "teensy:avr:teensy41"
                Name  = "Teensy 4.1"
                Core  = "teensy"
                Ext   = "hex" }
}

function Test-Target([string]$t) {
    if (-not $BOARDS.Contains($t)) {
        Write-Host "Unknown target '$t'. Valid: $($BOARDS.Keys -join ', ')" -ForegroundColor Red
        exit 1
    }
}

# compile one target into build\<target>\ ; returns $true on success
function Invoke-Build([string]$t) {
    Test-Target $t
    Assert-Arduino $t
    $outDir = "$Root\build\$t"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    Write-Host "`n=== Compiling $($BOARDS[$t].Name) ===" -ForegroundColor Cyan
    $fqbn = $BOARDS[$t].Fqbn
    & $CLI compile --fqbn $fqbn --config-dir $DATA --output-dir $outDir $Root 2>&1 |
        ForEach-Object { Write-Host "$_" }
    if ($LASTEXITCODE -eq 0) {
        Remove-Item "$outDir\*.eep" -ErrorAction SilentlyContinue   # Teensy: unused EEPROM image
        Write-Host "OK $t  ->  build\$t" -ForegroundColor Green
        return $true
    }
    Write-Host "FAILED $t" -ForegroundColor Red
    return $false
}

# upload build\<target>\<sketch>.ino.<ext> : BOOTSEL drive first (RP), else serial port
# (Teensy: arduino-cli hands the .hex to the Teensy Loader)
function Invoke-Upload([string]$t) {
    Test-Target $t
    $cfg    = $BOARDS[$t]
    $outDir = "$Root\build\$t"
    $uf2    = "$outDir\$Sketch.ino.$($cfg.Ext)"
    if (-not (Test-Path $uf2)) {
        Write-Host "No firmware at $uf2 - run .\scripts\build.ps1 $t first." -ForegroundColor Red
        exit 1
    }

    $drive = $null
    if ($cfg.Label) {
        $drive = Get-Volume -ErrorAction SilentlyContinue |
                 Where-Object { $_.FileSystemLabel -eq $cfg.Label -and $_.DriveLetter } |
                 Select-Object -First 1
    }
    if ($drive) {
        Write-Host "BOOTSEL drive $($drive.DriveLetter): found - copying UF2 ..." -ForegroundColor Cyan
        Copy-Item $uf2 "$($drive.DriveLetter):\"
        Write-Host "Upload OK" -ForegroundColor Green
        return
    }

    Assert-Arduino $t
    $board = & $CLI board list --config-dir $DATA 2>&1 |
             Where-Object { "$_" -match $cfg.Match } |
             Select-Object -First 1
    if (-not $board) {
        Write-Host "$($cfg.Name) not detected. Connect the board (RP: or hold BOOT and press RESET; Teensy: press the program button) and try again." -ForegroundColor Red
        exit 1
    }
    $port = ("$board" -split '\s+')[0]
    Write-Host "Uploading via $port ..." -ForegroundColor Cyan
    $fqbn = $cfg.Fqbn
    & $CLI upload --fqbn $fqbn --config-dir $DATA --port $port --input-dir $outDir $Root 2>&1 |
        ForEach-Object { Write-Host "$_" }
    if ($LASTEXITCODE -ne 0) { Write-Host "Upload failed." -ForegroundColor Red; exit 1 }
    Write-Host "Upload OK" -ForegroundColor Green
}
