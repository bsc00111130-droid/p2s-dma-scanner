$ErrorActionPreference = "Stop"

$GuiRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$HostName = "127.0.0.1"
$Port = 8765

function Find-Python {
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) {
        return $python.Source
    }

    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($py) {
        return $py.Source
    }

    throw "Python was not found on PATH."
}

function Find-FreePort {
    param([int]$StartPort)

    for ($candidate = $StartPort; $candidate -lt ($StartPort + 50); $candidate++) {
        $listener = Get-NetTCPConnection -LocalAddress $HostName -LocalPort $candidate -State Listen -ErrorAction SilentlyContinue
        if (-not $listener) {
            return $candidate
        }
    }

    throw "No free local port found near $StartPort."
}

$Python = Find-Python
$Port = Find-FreePort -StartPort $Port
$Url = "http://$HostName`:$Port/"

Write-Host "Starting Proc IOCTL Control Panel..."
Write-Host "URL: $Url"

$server = Start-Process -FilePath $Python `
    -ArgumentList @("serve_gui.py", "--host", $HostName, "--port", "$Port") `
    -WorkingDirectory $GuiRoot `
    -WindowStyle Hidden `
    -PassThru

try {
    $ready = $false
    for ($i = 0; $i -lt 40; $i++) {
        try {
            $response = Invoke-WebRequest -UseBasicParsing -Uri $Url -TimeoutSec 1
            if ($response.StatusCode -eq 200) {
                $ready = $true
                break
            }
        } catch {
            Start-Sleep -Milliseconds 150
        }
    }

    if (-not $ready) {
        throw "Local GUI server did not become ready."
    }

    Start-Process $Url
    Write-Host "Press Enter to stop the local GUI server."
    [void][Console]::ReadLine()
} finally {
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
    }
}
