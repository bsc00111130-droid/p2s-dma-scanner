$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$OutDir = Join-Path $Root "build\user"

function Find-VsDevCmd {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "vswhere.exe was not found. Install Visual Studio Build Tools 2022."
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installPath) {
        throw "MSVC x64 tools were not found. Install Visual Studio Build Tools 2022 with C++ tools."
    }

    $vsDevCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        throw "VsDevCmd.bat was not found under $installPath."
    }

    return $vsDevCmd
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$vsDevCmd = Find-VsDevCmd

$batch = @"
@echo off
setlocal
cd /d "$Root"
call "$vsDevCmd" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

cl /W4 /nologo /DUNICODE /D_UNICODE user\proc_ioctl_client.c /Fo:build\user\proc_ioctl_client.obj /Fe:build\user\proc_ioctl_client.exe
if errorlevel 1 exit /b %errorlevel%

cl /W4 /nologo /DUNICODE /D_UNICODE user\proc_ioctl_readmem.c /Fo:build\user\proc_ioctl_readmem.obj /Fe:build\user\proc_ioctl_readmem.exe
if errorlevel 1 exit /b %errorlevel%

cl /std:c++17 /EHsc /W4 /nologo /DUNICODE /D_UNICODE user\proc_ioctl_controller.cpp /Fo:build\user\proc_ioctl_controller.obj /Fe:build\user\proc_ioctl_controller.exe
if errorlevel 1 exit /b %errorlevel%

cl /std:c++17 /EHsc /W4 /nologo user\kalman_motion_demo.cpp /Fo:build\user\kalman_motion_demo.obj /Fe:build\user\kalman_motion_demo.exe
if errorlevel 1 exit /b %errorlevel%

build\user\kalman_motion_demo.exe --self-test
if errorlevel 1 exit /b %errorlevel%

build\user\proc_ioctl_readmem.exe
if %errorlevel% neq 50 exit /b %errorlevel%

echo User-mode build completed.
"@

$temp = Join-Path $env:TEMP ("proc_ioctl_build_user_" + [guid]::NewGuid().ToString("N") + ".cmd")
Set-Content -LiteralPath $temp -Value $batch -Encoding ASCII

try {
    & cmd.exe /d /s /c "`"$temp`""
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
}
