param(
    [Parameter(Position = 0)]
    [string]$Target,

    [Alias('d')]
    [string]$Device = 'RPI-RP2'
)

$flashableTargets = @(
    'pico-debug',
    'waveshare-rp2040-zero-debug'
)

function Show-Usage {
    Write-Host 'Usage: .\tools\flash.ps1 <target> [-Device <name>]'
    Write-Host ''
    Write-Host "Builds the selected target if needed, then copies its UF2 to the mounted device."
    Write-Host 'The default device name is RPI-RP2.'
    Write-Host ''
    Write-Host 'Flashable targets:'
    foreach ($name in $flashableTargets) {
        Write-Host "  $name"
    }
}

if (($args.Count -ne 0) -or [string]::IsNullOrWhiteSpace($Target) -or
    ($Target -notin $flashableTargets) -or [string]::IsNullOrWhiteSpace($Device)) {
    if (-not [string]::IsNullOrWhiteSpace($Target) -and
        $Target -notin $flashableTargets) {
        Write-Error "Target is not flashable: $Target" -ErrorAction Continue
        Write-Host ''
    }
    Show-Usage
    exit 2
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$firmware = Join-Path $repoRoot "build\$Target\microflow-raspberry-pi-pico.uf2"

foreach ($tool in @('cmake', 'ninja', 'arm-none-eabi-gcc')) {
    if (-not (Get-Command $tool -CommandType Application -ErrorAction SilentlyContinue)) {
        Write-Error "Required tool not found: $tool"
        exit 1
    }
}

if ([string]::IsNullOrWhiteSpace($env:PICO_SDK_PATH) -or
    (-not (Test-Path -LiteralPath (Join-Path $env:PICO_SDK_PATH 'external\pico_sdk_import.cmake') -PathType Leaf))) {
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        foreach ($candidate in @(
            (Join-Path $env:USERPROFILE '.pico-sdk\sdk\2.3.1'),
            (Join-Path $env:USERPROFILE 'sdk\pico-sdk-2.3.1'))) {
            if (Test-Path -LiteralPath (Join-Path $candidate 'external\pico_sdk_import.cmake') -PathType Leaf) {
                $env:PICO_SDK_PATH = $candidate
                Write-Host "Using Pico SDK: $candidate"
                break
            }
        }
    }
}
if ([string]::IsNullOrWhiteSpace($env:PICO_SDK_PATH) -or
    (-not (Test-Path -LiteralPath (Join-Path $env:PICO_SDK_PATH 'external\pico_sdk_import.cmake') -PathType Leaf))) {
    Write-Error 'Pico SDK 2.3.1 not found; set PICO_SDK_PATH.'
    exit 1
}

$buildFile = Join-Path $repoRoot "build\$Target\build.ninja"
Push-Location $repoRoot
try {
    if (-not (Test-Path -LiteralPath $buildFile -PathType Leaf)) {
        & cmake --preset $Target
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    & cmake --build --preset $Target
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    Pop-Location
}

$volume = Get-Volume -FileSystemLabel $Device -ErrorAction SilentlyContinue |
    Where-Object DriveLetter |
    Select-Object -First 1
if ($null -eq $volume) {
    Write-Error "Device is not mounted with volume label: $Device"
    exit 1
}

$destination = "$($volume.DriveLetter):\"
Write-Host "Flashing $Target to $destination"
Copy-Item -LiteralPath $firmware -Destination $destination
Write-Host 'Flash complete.'
