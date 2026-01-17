@echo off
call C:\Espressif\frameworks\esp-idf-v5.5.1\export.bat
cd C:\Esp\Treadmill\Consola
idf.py -p COM8 flash monitor
