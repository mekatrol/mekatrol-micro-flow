param(
    [ValidateSet('host-debug','host-release')]
    [string]$Preset = 'host-debug',
    [string]$Test
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& (Join-Path $PSScriptRoot 'build.ps1') -Preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$Args = @('--test-dir', (Join-Path $Root "build/$Preset"), '--output-on-failure')
if ($Test) { $Args += @('-R', $Test) }
& ctest @Args
exit $LASTEXITCODE
