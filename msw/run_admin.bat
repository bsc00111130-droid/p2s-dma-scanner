@echo off
fltmc >nul 2>&1 || (
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs -WindowStyle Hidden"
    exit /b
)
cd /d C:\msw
python se_debug_launcher.py
echo.
echo Press any key to exit...
pause >nul
