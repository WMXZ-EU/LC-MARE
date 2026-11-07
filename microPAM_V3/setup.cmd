@echo off
rem setup.cmd - create the Python environment for the workbench by double-click (runs scripts\setup.ps1)
rem Arguments are passed on, e.g.  setup.cmd -Force
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup.ps1" %*
pause
