param(
    [ValidateSet('host-debug','host-release','ch32v003-debug','ch32v003-release')]
    [string]$Preset = 'ch32v003-release',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw 'cmake is required' }
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { throw 'ninja is required' }
if ($Clean) { & cmake -E remove_directory (Join-Path $Root "build/$Preset") }
& cmake --preset $Preset -S $Root
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Push-Location $Root
try { & cmake --build --preset $Preset } finally { Pop-Location }
exit $LASTEXITCODE
