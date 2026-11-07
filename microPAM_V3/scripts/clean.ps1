# clean.ps1 - remove compiled output under build\<target>\
# Usage:
#   .\scripts\clean.ps1            # both targets
#   .\scripts\clean.ps1 rp2040

param([string]$Target = "all")
. "$PSScriptRoot\common.ps1"

$targets = if ($Target -eq "all") { @($BOARDS.Keys) } else { @($Target) }
foreach ($t in $targets) {
    Test-Target $t
    $dir = "$Root\build\$t"
    if (Test-Path $dir) {
        Remove-Item "$dir\*" -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Cleaned build\$t" -ForegroundColor Green
    }
}
