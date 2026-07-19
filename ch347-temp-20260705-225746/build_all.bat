@echo off
setlocal enabledelayedexpansion
set DIR=%~dp0
set BUILD=%DIR%build

if not exist "%BUILD%" mkdir "%BUILD%"

echo === P2S Full Build ===
echo.

echo [1/2] Building kernel driver...
cl.exe /nologo /O2 /W2 /GS- /Gz /LD /I"%DIR%include" /D_WIN32_WINNT=0x0A00 /std:c11 ^
    "%DIR%driver\proc_ioctl_driver.c" ^
    /link /NODEFAULTLIB /SUBSYSTEM:NATIVE /DRIVER /ENTRY:DriverEntry ^
    /OUT:"%BUILD%\p2s.sys" 2>&1
if %ERRORLEVEL% equ 0 ( echo [+] driver: %BUILD%\p2s.sys
) else ( echo [!] driver build failed )

echo [2/2] Building user-mode programs...
set CFLAGS=/nologo /O2 /W2 /EHsc /std:c++17 /I"%DIR%include"

:: GUI
cl.exe %CFLAGS% "%DIR%user\p2s_gui.cpp" /Fe"%BUILD%\p2s_gui.exe" /link /SUBSYSTEM:WINDOWS comctl32.lib
if %ERRORLEVEL% equ 0 ( echo [+] GUI: %BUILD%\p2s_gui.exe
) else ( echo [!] GUI build failed )

:: CLI
cl.exe %CFLAGS% "%DIR%user\proc_ioctl_controller.cpp" /Fe"%BUILD%\p2s_cli.exe" /link /SUBSYSTEM:CONSOLE
if %ERRORLEVEL% equ 0 ( echo [+] CLI: %BUILD%\p2s_cli.exe
) else ( echo [!] CLI build failed )

echo.
echo === Build complete ===
echo  driver: %BUILD%\p2s.sys
echo  GUI:    %BUILD%\p2s_gui.exe
echo  CLI:    %BUILD%\p2s_cli.exe
echo.
echo === Install and run ===
echo sc create P2S type= kernel binPath= %BUILD%\p2s.sys
echo sc start P2S
echo %BUILD%\p2s_gui.exe
