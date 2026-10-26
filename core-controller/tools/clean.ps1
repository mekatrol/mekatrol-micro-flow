$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $repoRoot 'build'
& cmake -E remove_directory $buildDirectory
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
