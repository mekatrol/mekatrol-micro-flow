$buildDirectory = Join-Path $PSScriptRoot 'build'
& cmake -E remove_directory $buildDirectory
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
