# Python for LC_Mare

## Installation
From powershell

To find python
> cmd /c "where python"

To check path
> $env:Path -spilt ";"

To create venv
> python -m venv .venv

Activate
> .venv/Scripts/Activate

To install required modules
> pip install pyserial scipy matplotlib numba sounddevice

To run LC-MARE GUI from console:
> python LC_Mare.py

## Scripts
- microPAM.py: module to be imported into scripts
- LC-Mare.py: GUI to address some basic operations

## Executable
A compiled version of LC_Mare.py may be found in the release section of this repository 

https://github.com/WMXZ-EU/LC-MARE/releases/download/1.0.0/LC_Mare.zip
