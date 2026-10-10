@echo off
cd /d "%~dp0"
py -3 epochpact_ui.py
if errorlevel 1 pause
