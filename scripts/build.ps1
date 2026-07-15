# Configure + build sipdemo on Windows (MSVC).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Test
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Release
#
# Requires: Visual Studio 2022 Build Tools (C++), CMake on PATH or in Program Files.

param(
    [ValidateSet("RelWithDebInfo", "Release", "Debug")]
    [string]$Config = "RelWithDebInfo",
    [switch]$Test,
    [switch]$Clean
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
if (-not $cmake) {
    Fail "CMake not found. Install with: winget install -e --id Kitware.CMake"
}

$vcvars = Find-VcVars64
if (-not $vcvars) {
    Fail "MSVC toolchain not found. Install VS 2022 Build Tools with the C++ workload."
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning $BuildDir ..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

Write-Host "=== sipdemo build ===" -ForegroundColor Cyan
Write-Host "Root   : $Root"
Write-Host "Config : $Config"
Write-Host "CMake  : $cmake"
Write-Host "vcvars : $vcvars"
Write-Host ""

# Run configure + build inside a Developer environment so cl.exe is on PATH.
$configure = if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) { "1" } else { "0" }

$inner = @"
@echo off
call "$vcvars"
if errorlevel 1 exit /b 1
cd /d "$Root"
if "$configure"=="1" (
  echo Configuring...
  "$cmake" -S . -B build -G "Visual Studio 17 2022" -A x64
  if errorlevel 1 exit /b 1
) else (
  echo Reusing existing build\CMakeCache.txt
)
echo Building ($Config)...
"$cmake" --build build --config $Config
if errorlevel 1 exit /b 1
"@

if ($Test) {
    $inner += @"

echo Running tests...
"$cmake" -E chdir build ctest -C $Config --output-on-failure
if errorlevel 1 exit /b 1
"@
}

$batPath = Join-Path $env:TEMP "sipdemo-build-$PID.bat"
Set-Content -Path $batPath -Value $inner -Encoding ASCII
try {
    & cmd /c $batPath
    if ($LASTEXITCODE -ne 0) { Fail "Build failed (exit $LASTEXITCODE)." }
} finally {
    Remove-Item $batPath -Force -ErrorAction SilentlyContinue
}

$exe = Join-Path $Root "build\$Config\sipdemo.exe"
if (-not (Test-Path $exe)) {
    Fail "Build finished but $exe is missing."
}

Write-Host "`nOK: $exe" -ForegroundColor Green
Write-Host "Run demo:  powershell -ExecutionPolicy Bypass -File .\scripts\run-demo.ps1"
