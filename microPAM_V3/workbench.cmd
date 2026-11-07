@echo off
rem workbench.cmd - start the microPAM_V3 workbench by double-click (runs scripts\workbench.ps1)
rem Arguments are passed on, e.g.  workbench.cmd -Dev
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\workbench.ps1" %*
if errorlevel 1 pause
