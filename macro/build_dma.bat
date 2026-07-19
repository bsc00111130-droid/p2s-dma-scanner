@echo off
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set SRC_DIR=%SCRIPT_DIR%dma_reader
set BUILD_DIR=%SRC_DIR%\x64\Release

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [1/2] Compiling C++ DMA reader module...
cl.exe /nologo /O2 /GL /arch:AVX2 /GS- /Gy /EHsc /LD /I"%SRC_DIR%" /Fe"%BUILD_DIR%\dmareader.dll" /Fo"%BUILD_DIR%\dmareader.obj" ^
    /D_WIN32_WINNT=0x0A00 /std:c++20 "%SRC_DIR%\reader.h" "%SRC_DIR%\ptr_chain.h" ^
    /link /LTCG /OPT:REF /OPT:ICF /NODEFAULTLIB:msvcrt.lib /DLL /OUT:"%BUILD_DIR%\dmareader.dll"

if %ERRORLEVEL% neq 0 (
    echo [!] Compilation failed - falling back to Python mode
    exit /b 1
)

echo [+] DLL built: %BUILD_DIR%\dmareader.dll
echo [2/2] Verifying...
if exist "%BUILD_DIR%\dmareader.dll" (
    echo [+] Module ready. C++ acceleration enabled.
) else (
    echo [!] DLL not found - will use Python fallback
)
