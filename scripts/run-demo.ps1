# Starts the Vosk AI gateway + sipdemo media server, then tears both down on quit.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\run-demo.ps1
#
# Softphone: dial sip:127.0.0.1  (MicroSIP: omit :5060)
# Press Ctrl+C to stop both processes.

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$GatewayDir = Join-Path $Root "gateway"
$Uvicorn = Join-Path $GatewayDir ".venv\Scripts\uvicorn.exe"

$SipdemoCandidates = @(
    (Join-Path $Root "build\RelWithDebInfo\sipdemo.exe"),
    (Join-Path $Root "build\Release\sipdemo.exe"),
    (Join-Path $Root "build\Debug\sipdemo.exe")
)
$SipdemoExe = $SipdemoCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

$ModelCandidates = @(
    (Join-Path $GatewayDir "vosk-model-small-en-us-0.15"),
    (Join-Path $GatewayDir "model")
)
$ModelPath = $ModelCandidates | Where-Object {
    (Test-Path $_ -PathType Container) -and (Test-Path (Join-Path $_ "am"))
} | Select-Object -First 1

function Fail([string]$msg) {
    Write-Host "ERROR: $msg" -ForegroundColor Red
    exit 1
}

if (-not $SipdemoExe) {
    Fail "sipdemo.exe not found under build\. Build first:`n  cmake -S . -B build && cmake --build build --config RelWithDebInfo"
}
if (-not (Test-Path $Uvicorn)) {
    Fail "Gateway venv missing. From gateway\:`n  python -m venv .venv`n  .\.venv\Scripts\Activate.ps1`n  pip install -r requirements.txt"
}
if (-not $ModelPath) {
    Fail "Vosk model not found. Download vosk-model-small-en-us-0.15 into gateway\ (see gateway\model\README.md)"
}

$script:gatewayProc = $null
$script:sipdemoProc = $null

function Stop-Demo {
    Write-Host "`nStopping demo..." -ForegroundColor Yellow
    foreach ($p in @($script:sipdemoProc, $script:gatewayProc)) {
        if ($null -eq $p) { continue }
        try {
            if (-not $p.HasExited) {
                Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            }
        } catch {}
    }
    Get-CimInstance Win32_Process -Filter "Name='python.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and ($_.CommandLine -match 'uvicorn.*app:app') } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Get-Process sipdemo -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Write-Host "Stopped." -ForegroundColor Green
}

# Ensure Ctrl+C runs cleanup (PowerShell runs finally on break).
$null = Register-EngineEvent PowerShell.Exiting -Action { Stop-Demo } -ErrorAction SilentlyContinue

try {
    Write-Host "=== sipdemo local demo ===" -ForegroundColor Cyan
    Write-Host "Gateway model : $ModelPath"
    Write-Host "Media server  : $SipdemoExe"
    Write-Host "Dial from softphone: sip:127.0.0.1  (MicroSIP: omit :5060)"
    Write-Host "Press Ctrl+C to quit.`n" -ForegroundColor DarkGray

    $env:VOSK_MODEL_PATH = $ModelPath
    $env:PYTHONUNBUFFERED = "1"

    $script:gatewayProc = Start-Process -FilePath $Uvicorn `
        -ArgumentList @("app:app", "--host", "127.0.0.1", "--port", "8080") `
        -WorkingDirectory $GatewayDir `
        -PassThru -NoNewWindow

    Start-Sleep -Seconds 2
    if ($script:gatewayProc.HasExited) {
        Fail "Gateway exited immediately (exit $($script:gatewayProc.ExitCode)). Check model path / venv."
    }

    $script:sipdemoProc = Start-Process -FilePath $SipdemoExe `
        -ArgumentList @(
            "--ip", "127.0.0.1",
            "--sip-port", "5060",
            "--rtp-port", "40000",
            "--ws-url", "ws://127.0.0.1:8080/stream"
        ) `
        -WorkingDirectory $Root `
        -PassThru -NoNewWindow

    Start-Sleep -Seconds 1
    if ($script:sipdemoProc.HasExited) {
        Fail "sipdemo exited immediately (exit $($script:sipdemoProc.ExitCode)). Is port 5060 free?"
    }

    Write-Host "Both processes running. Gateway pid=$($script:gatewayProc.Id)  sipdemo pid=$($script:sipdemoProc.Id)" -ForegroundColor Green
    Write-Host "Waiting until you press Ctrl+C...`n"

    while ($true) {
        if ($script:gatewayProc.HasExited) {
            Write-Host "Gateway exited (code $($script:gatewayProc.ExitCode))." -ForegroundColor Red
            break
        }
        if ($script:sipdemoProc.HasExited) {
            Write-Host "sipdemo exited (code $($script:sipdemoProc.ExitCode))." -ForegroundColor Red
            break
        }
        Start-Sleep -Seconds 1
    }
}
finally {
    Stop-Demo
}
