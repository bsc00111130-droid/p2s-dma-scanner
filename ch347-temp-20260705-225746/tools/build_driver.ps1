$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$OutDir = Join-Path $Root "build\driver"
$PreferredKit = "10.0.19041.0"

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

function Find-WdkKit {
    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"
    $includeRoot = Join-Path $kitsRoot "Include"
    $libRoot = Join-Path $kitsRoot "Lib"

    if (Test-Path -LiteralPath (Join-Path $includeRoot "$PreferredKit\km")) {
        return $PreferredKit
    }

    $kits = Get-ChildItem -LiteralPath $includeRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "km") } |
        Sort-Object Name -Descending

    foreach ($kit in $kits) {
        if (Test-Path -LiteralPath (Join-Path $libRoot "$($kit.Name)\km\x64")) {
            return $kit.Name
        }
    }

    throw "Windows Driver Kit headers/libs were not found."
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$vsDevCmd = Find-VsDevCmd
$kitVersion = Find-WdkKit
$kitRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10"
$kmInclude = Join-Path $kitRoot "Include\$kitVersion\km"
$kmLib = Join-Path $kitRoot "Lib\$kitVersion\km\x64"

$batch = @"
@echo off
setlocal
cd /d "$Root"
call "$vsDevCmd" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

cl /nologo /c /W4 /wd4117 /kernel /GS /GR- /Gz /Zc:wchar_t /D_AMD64_=1 /D_WIN64 /DWIN32=100 /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /DNTDDI_VERSION=0x0A000000 /D_KERNEL_MODE /DPOOL_NX_OPTIN=1 /I"$kmInclude" driver\proc_ioctl_driver.c /Fo:build\driver\proc_ioctl_driver.obj
if errorlevel 1 exit /b %errorlevel%

link /nologo /driver /subsystem:native /machine:x64 /entry:DriverEntry /out:build\driver\proc_ioctl_driver.sys build\driver\proc_ioctl_driver.obj /libpath:"$kmLib" ntoskrnl.lib hal.lib BufferOverflowK.lib
if errorlevel 1 exit /b %errorlevel%

echo Driver build completed with WDK $kitVersion.
"@

$temp = Join-Path $env:TEMP ("proc_ioctl_build_driver_" + [guid]::NewGuid().ToString("N") + ".cmd")
Set-Content -LiteralPath $temp -Value $batch -Encoding ASCII

try {
    & cmd.exe /d /s /c "`"$temp`""
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
}
