param(
    [Parameter(Position = 0)]
    [string]$Target
)

$availableTargets = @(
    'host-debug',
    'host-release',
    'pico-debug',
    'waveshare-rp2040-zero-debug',
    'pico2-arm-debug',
    'pico2-riscv-debug'
)

function Initialize-PicoSdk {
    if (-not [string]::IsNullOrWhiteSpace($env:PICO_SDK_PATH)) {
        $import = Join-Path $env:PICO_SDK_PATH 'external\pico_sdk_import.cmake'
        if (Test-Path -LiteralPath $import -PathType Leaf) { return $true }
    }
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        foreach ($candidate in @(
            (Join-Path $env:USERPROFILE '.pico-sdk\sdk\2.3.1'),
            (Join-Path $env:USERPROFILE 'sdk\pico-sdk-2.3.1'))) {
            $import = Join-Path $candidate 'external\pico_sdk_import.cmake'
            if (Test-Path -LiteralPath $import -PathType Leaf) {
                $env:PICO_SDK_PATH = $candidate
                Write-Host "Using Pico SDK: $candidate"
                return $true
            }
        }
    }
    Write-Error 'Pico SDK 2.3.1 not found; set PICO_SDK_PATH.'
    return $false
}

function Show-Usage {
    Write-Host 'Usage: .\tools\build.ps1 <target>'
    Write-Host ''
    Write-Host 'Formats the source tree, deletes build/, configures the selected target,'
    Write-Host 'and builds it.'
    Write-Host ''
    Write-Host 'Available targets:'
    foreach ($name in $availableTargets) {
        Write-Host "  $name"
    }
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

$requiredCompiler = switch ($Target) {
    { $_ -in 'pico-debug', 'waveshare-rp2040-zero-debug', 'pico2-arm-debug' } {
        'arm-none-eabi-gcc'
    }
    'pico2-riscv-debug' { 'riscv32-unknown-elf-gcc' }
    default { $null }
}

$requiredTools = @('cmake', 'ninja')
if ($null -ne $requiredCompiler) {
    $requiredTools += $requiredCompiler
}
foreach ($tool in $requiredTools) {
    if (-not (Get-Command $tool -CommandType Application -ErrorAction SilentlyContinue)) {
        Write-Error "Required tool not found: $tool"
        exit 1
    }
}
$clangFormat = $null
foreach ($candidate in @(
    'clang-format',
    'clang-format-20',
    'clang-format-19',
    'clang-format-18',
    'clang-format-17',
    'clang-format-16',
    'clang-format-15')) {
    $clangFormat = Get-Command $candidate -CommandType Application -ErrorAction SilentlyContinue
    if ($null -ne $clangFormat) { break }
}
if ($null -eq $clangFormat) {
    Write-Error 'Required tool not found: clang-format 15 or newer'
    exit 1
}
$pythonAvailable =
    ($null -ne (Get-Command py -CommandType Application -ErrorAction SilentlyContinue)) -or
    ($null -ne (Get-Command python -CommandType Application -ErrorAction SilentlyContinue))
if (-not $pythonAvailable) {
    Write-Error 'Required tool not found: Python 3'
    exit 1
}
if (($Target -in 'pico-debug', 'waveshare-rp2040-zero-debug') -and
    (-not (Initialize-PicoSdk))) {
    exit 1
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    Write-Host 'Formatting source files...'
    $sourceFiles = @(Get-ChildItem -Path 'src', 'tests' -Recurse -File |
        Where-Object { $_.Extension -in '.c', '.h', '.cc', '.cpp', '.hpp' })
    foreach ($sourceFile in $sourceFiles) {
        & $clangFormat.Source -i -- $sourceFile.FullName
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    & (Join-Path $PSScriptRoot 'format-c-functions.ps1')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & cmake -E remove_directory (Join-Path $repoRoot 'build')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cmake --preset $Target
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cmake --build --preset $Target
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    Pop-Location
}
