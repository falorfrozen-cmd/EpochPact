@echo off
cd /d "%~dp0"
py -3 epochpact_ui.py --preview --port 17885
if errorlevel 1 pause
