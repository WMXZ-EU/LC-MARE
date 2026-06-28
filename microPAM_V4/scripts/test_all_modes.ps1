#!/usr/bin/env pwsh
# test_all_modes.ps1
# Cycle through PROC_MODE 0..4: compile, flash, wait 5 minutes each.
# Restores PROC_MODE 4 when done.

$Root    = Split-Path $PSScriptRoot -Parent
$Config  = Join-Path $Root "config.h"
$Flash   = Join-Path $Root "scripts\flash_teensy.ps1"
$WAIT_S  = 300   # 5 minutes per mode

function Set-ProcMode([int]$mode) {
    $text = Get-Content $Config -Raw
    $text = $text -replace '(#define\s+PROC_MODE\s+)\d+', "`${1}$mode"
    Set-Content $Config $text -NoNewline
    Write-Host "  config.h -> PROC_MODE = $mode"
}

function Get-ProcMode {
    $text = Get-Content $Config -Raw
    if ($text -match '#define\s+PROC_MODE\s+(\d+)') { return [int]$Matches[1] }
    return -1
}

$original = Get-ProcMode
Write-Host "Original PROC_MODE = $original"
Write-Host ""

$modes = 0..4
$results = @()

foreach ($mode in $modes) {
    Write-Host ("=" * 60)
    Write-Host "  PROC_MODE $mode"
    Write-Host ("=" * 60)

    # patch
    Set-ProcMode $mode

    # compile + flash
    $t0 = Get-Date
    & $Flash
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "  Flash failed (exit $LASTEXITCODE)"
        $results += [PSCustomObject]@{ Mode=$mode; Status="FLASH FAILED"; Duration=0 }
        continue
    }

    Write-Host ""
    Write-Host "  Flashed OK. Recording for $WAIT_S s ..."

    # countdown
    for ($s = $WAIT_S; $s -gt 0; $s -= 10) {
        Write-Host ("    {0:mm\:ss} remaining ..." -f [timespan]::FromSeconds($s))
        Start-Sleep -Seconds ([math]::Min(10, $s))
    }

    $elapsed = [int]((Get-Date) - $t0).TotalSeconds
    Write-Host "  Done (${elapsed}s total for mode $mode)."
    $results += [PSCustomObject]@{ Mode=$mode; Status="OK"; Duration=$elapsed }
    Write-Host ""
}

# restore original mode
Write-Host ("=" * 60)
Write-Host "  Restoring PROC_MODE $original"
Set-ProcMode $original
& $Flash
Write-Host ""

# summary
Write-Host ("=" * 60)
Write-Host "  SUMMARY"
Write-Host ("=" * 60)
$results | Format-Table -AutoSize
