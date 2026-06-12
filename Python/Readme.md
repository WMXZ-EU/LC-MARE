## installation
From powershell

To find python
> cmd /c "where python"

To check path
> $env:Path -spilt ";"

To create venv
> python -m venv .venv

Activate
> .venv/Scripts/Activate

To install serial
> pip install pyserial
> pip install scipy matplotlib numba
> pip install sounddevice

To run LC-MARE GUI from console:
> python LC-Mare.py

## Programs
- microPAM.py: module to be imported into scripts
- LC-Mare.py: GUI to address some basic operations
