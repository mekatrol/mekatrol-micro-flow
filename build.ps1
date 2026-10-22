param(
    [Parameter(Position = 0)]
    [string]$Target
)

$availableTargets = @(
    'host-debug',
    'host-release'
)

function Show-Usage {
    Write-Host 'Usage: .\build.ps1 <target>'
    Write-Host ''
    Write-Host 'Deletes the build directory, configures the selected target, and builds it.'
    Write-Host ''
    Write-Host 'Available targets:'
    Write-Host '  host-debug    Debug build with tests and supported sanitizers'
    Write-Host '  host-release  Optimized release build with tests'
}

if (($args.Count -ne 0) -or ([string]::IsNullOrWhiteSpace($Target)) -or
    ($Target -notin $availableTargets)) {
    if (-not [string]::IsNullOrWhiteSpace($Target) -and
        $Target -notin $availableTargets) {
        Write-Error "Unknown target: $Target" -ErrorAction Continue
        Write-Host ''
    }

    Show-Usage
    exit 2
}

Push-Location $PSScriptRoot
try {
    & cmake -E remove_directory (Join-Path $PSScriptRoot 'build')
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & cmake --preset $Target
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & cmake --build --preset $Target
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
