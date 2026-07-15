# Configure + run unit tests (GoogleTest via CTest).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\test.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\test.ps1 -Config Release
#   powershell -ExecutionPolicy Bypass -File scripts\test.ps1 -Filter Dtmf
#
# Builds sipdemo_tests if needed, then runs ctest.
# Equivalent:
#   cmake --build build --config RelWithDebInfo --target sipdemo_tests
#   ctest --test-dir build -C RelWithDebInfo --output-on-failure
#   cmake --build build --config RelWithDebInfo --target check

param(
    [ValidateSet("RelWithDebInfo", "Release", "Debug")]
    [string]$Config = "RelWithDebInfo",
    [string]$Filter = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build"

function Fail([string]$msg) {
    Write-Host "ERROR: $msg" -ForegroundColor Red
    exit 1
}

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $pf = Join-Path ${env:ProgramFiles} "CMake\bin\cmake.exe"
    if (Test-Path $pf) { return $pf }
    return $null
}

function Find-VcVars64 {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    $install = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $install) { return $null }
    $vcvars = Join-Path $install "VC\Auxiliary\Build\vcvars64.bat"
    if (Test-Path $vcvars) { return $vcvars }
    return $null
}

$cmake = Find-CMake
if (-not $cmake) { Fail "CMake not found." }
$vcvars = Find-VcVars64
if (-not $vcvars) { Fail "MSVC toolchain not found." }

if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Host "No build tree yet - running scripts\build.ps1 first..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot "build.ps1") -Config $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$ctestExtra = ""
if ($Filter) {
    $ctestExtra = " -R $Filter"
}

$lines = @(
    "@echo off",
    "call `"$vcvars`"",
    "if errorlevel 1 exit /b 1",
    "cd /d `"$Root`"",
    "echo Building sipdemo_tests ($Config)...",
    "`"$cmake`" --build build --config $Config --target sipdemo_tests",
    "if errorlevel 1 exit /b 1",
    "echo.",
    "echo Running ctest...",
    "`"$cmake`" -E chdir build ctest -C $Config --output-on-failure --timeout 60$ctestExtra",
    "if errorlevel 1 exit /b 1"
)
$batPath = Join-Path $env:TEMP "sipdemo-test-$PID.bat"
Set-Content -Path $batPath -Value ($lines -join "`r`n") -Encoding ASCII
try {
    & cmd /c $batPath
    if ($LASTEXITCODE -ne 0) { Fail "Tests failed (exit $LASTEXITCODE)." }
} finally {
    Remove-Item $batPath -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "All requested tests passed." -ForegroundColor Green
