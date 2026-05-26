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

To run LC-MARE GUI from console:
> python LC-Mare-GUI.py

For other programs
> pip install scipy matplotlib numba

## Programs
- microPAM.py: module to be imported into scripts
- LC-Mare-GUI.py: GUI to setup LC-Mare firmware (adalogger_V3)
- LC_Mare-View.py: 