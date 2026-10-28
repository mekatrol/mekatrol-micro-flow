param(
    [ValidateSet('ch32v003-debug','ch32v003-release')]
    [string]$Preset = 'ch32v003-release'
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not (Get-Command wlink -ErrorAction SilentlyContinue)) { throw 'wlink is required; see docs/DEPENDENCIES.md' }
& (Join-Path $PSScriptRoot 'build.ps1') -Preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$Elf = Join-Path $Root "build/$Preset/output-expander.elf"
if (-not (Test-Path -LiteralPath $Elf -PathType Leaf)) { throw "missing firmware ELF: $Elf" }
& wlink -v flash --chip CH32V003 --speed low $Elf
exit $LASTEXITCODE
