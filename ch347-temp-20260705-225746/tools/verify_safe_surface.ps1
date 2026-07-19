$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

$activeFiles = @(
    "driver\proc_ioctl_driver.c",
    "include\proc_ioctl_shared.h",
    "user\proc_ioctl_client.c",
    "user\proc_ioctl_controller.cpp",
    "user\kalman_motion_filter.hpp",
    "user\kalman_motion_demo.cpp"
)

$blockedPatterns = @(
    "MmCopyVirtualMemory",
    "IOCTL_PROC_IOCTL_READ_PROCESS_MEMORY",
    "PROC_IOCTL_READMEM_REQUEST",
    "PROC_IOCTL_READMEM_RESPONSE",
    "ProcIoctlHandleReadProcessMemory",
    "__readcr3",
    "DirectoryTableBase",
    "MmCopyMemory"
)

$failures = New-Object System.Collections.Generic.List[string]

foreach ($relativePath in $activeFiles) {
    $path = Join-Path $Root $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        $failures.Add("Missing active file: $relativePath")
        continue
    }

    $content = Get-Content -LiteralPath $path -Raw
    foreach ($pattern in $blockedPatterns) {
        if ($content -match [regex]::Escape($pattern)) {
            $failures.Add("Blocked pattern '$pattern' found in $relativePath")
        }
    }
}

$sharedHeader = Get-Content -LiteralPath (Join-Path $Root "include\proc_ioctl_shared.h") -Raw
$driverSource = Get-Content -LiteralPath (Join-Path $Root "driver\proc_ioctl_driver.c") -Raw
$readmemStub = Get-Content -LiteralPath (Join-Path $Root "user\proc_ioctl_readmem.c") -Raw

if ($sharedHeader -notmatch "#define\s+PROC_IOCTL_ENABLE_UNSAFE_READMEM\s+0") {
    $failures.Add("Shared header does not keep PROC_IOCTL_ENABLE_UNSAFE_READMEM at 0")
}

if ($driverSource -notmatch "#define\s+PROC_IOCTL_ENABLE_UNSAFE_READMEM\s+0") {
    $failures.Add("Driver source does not keep PROC_IOCTL_ENABLE_UNSAFE_READMEM at 0")
}

if ($driverSource -notmatch "#if\s+PROC_IOCTL_ENABLE_UNSAFE_READMEM\s*\r?\n#error") {
    $failures.Add("Driver source does not hard-fail if unsafe readmem is enabled")
}

if ($readmemStub -notmatch "disabled in this build") {
    $failures.Add("Read-memory compatibility tool does not clearly report disabled state")
}

if ($failures.Count -gt 0) {
    Write-Host "SAFE SURFACE AUDIT FAILED" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "SAFE SURFACE AUDIT PASSED"
Write-Host "Active driver/controller files contain no process-memory read dispatch or copy path."
