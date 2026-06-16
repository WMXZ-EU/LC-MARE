# LC-MARE
Low-Cost Marine Acoustic REcorder. See detailed info in different folders
 
Actual preferred firmware verson is adalogger_V3


## Content
### 3D-models
contains some stl files for 3D printing
### adalogger_V2
first LC-Mare firmware uses only adalogger RP2040. Suitable for single hydrophone and sampling frequency up to 96 kHz
### adalogger_V3
improved LC-Mare firmware 
- for adalogger RP2040 (single hydrophone up to 96 kHz sampling frequency)
- for rp2350 based adafruit feather (stereo and up to 384 kHz sampling frequency)
### microPAM_V4
development version for three MCU types (RP2040, RP2350, Teensy4.1)
Work in Progress
### Documents
different files describing project and hardware construction
### KICAD
hardware desigh files for
- ADC (ADC_V2)
- preamp (PreampV5-mare)
- Base board (LC_PAMV4b)
- Libraries (for KICAD)
### Python
contains python based setup and analysis scripts

Of importance is a GUI (LC_Mare.py) that provides access to basic operations, like inspecting LC_mare file parameters, visualizing time series and spectra, to configure the firmware, to convert compressed to wav files, and to upload latest firmware

An compiled version (.exe) with supporting libraries may be found in

https://github.com/WMXZ-EU/LC-MARE/releases/download/LC_Mare_v1.0.0/LC_Mare.zip
