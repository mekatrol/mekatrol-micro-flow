param()
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& cmake -E remove_directory (Join-Path $Root 'build')
exit $LASTEXITCODE
