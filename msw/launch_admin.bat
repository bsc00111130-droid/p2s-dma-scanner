@echo off
:: MSWloader Launcher — Run as Admin
fltmc >nul 2>&1 || (
    echo Requesting administrator privileges...
    powershell -Command "Start-Process -FilePath '%~0' -Verb RunAs"
    exit /b
)
cd /d C:\msw
python bypass_launcher.py
pause
