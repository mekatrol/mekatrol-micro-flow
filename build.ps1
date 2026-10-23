param(
    [Parameter(Position = 0)]
    [string]$Target
)

$availableTargets = @(
    'host-debug',
    'host-release',
    'waveshare-rp2040-zero-debug',
    'raspberry-pi-pico-2-arm-debug',
    'raspberry-pi-pico-2-riscv-debug',
    'waveshare-esp32-c6-zero-debug',
    'waveshare-esp32-s3-zero-debug'
)

function Add-InstalledToolToPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,

        [switch]$PreferDiscovered
    )

    if (-not $PreferDiscovered -and
        (Get-Command $Command -CommandType Application -ErrorAction SilentlyContinue)) {
        return $true
    }

    $searchRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:IDF_TOOLS_PATH)) {
        $searchRoots += $env:IDF_TOOLS_PATH
        $searchRoots += Join-Path $env:IDF_TOOLS_PATH 'tools'
    }
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        $searchRoots += Join-Path $env:USERPROFILE '.espressif\tools'
        $searchRoots += Join-Path $env:USERPROFILE '.pico-sdk'
        $searchRoots += Join-Path $env:USERPROFILE '.mcuxpressotools'
        $searchRoots += Join-Path $env:USERPROFILE '.local\bin'
        $searchRoots += Join-Path $env:USERPROFILE 'tools'
    }
    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        $searchRoots += Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links'
    }

    $candidateNames = @($Command, "$Command.exe", "$Command.cmd", "$Command.ps1", "$Command.py")
    foreach ($searchRoot in ($searchRoots | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $searchRoot -PathType Container)) {
            continue
        }

        $tool = Get-ChildItem -LiteralPath $searchRoot -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -in $candidateNames } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($null -ne $tool) {
            $env:Path = "$($tool.DirectoryName)$([IO.Path]::PathSeparator)$env:Path"
            Write-Host "Using discovered ${Command}: $($tool.FullName)"
            return $true
        }
    }

    if (Get-Command $Command -CommandType Application -ErrorAction SilentlyContinue) {
        return $true
    }

    Write-Error "Required tool not found: $Command. Install it or add its directory to PATH, then try again."
    return $false
}

function Initialize-BuildTools {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildTarget
    )

    if (-not (Add-InstalledToolToPath 'ninja')) {
        return $false
    }

    switch ($BuildTarget) {
        'waveshare-rp2040-zero-debug' {
            if (-not (Add-InstalledToolToPath 'arm-none-eabi-gcc')) { return $false }
        }
        'raspberry-pi-pico-2-arm-debug' {
            if (-not (Add-InstalledToolToPath 'arm-none-eabi-gcc')) { return $false }
        }
        'raspberry-pi-pico-2-riscv-debug' {
            if (-not (Add-InstalledToolToPath 'riscv32-unknown-elf-gcc')) { return $false }
        }
        'waveshare-esp32-c6-zero-debug' {
            if (-not (Add-InstalledToolToPath 'riscv32-esp-elf-gcc')) { return $false }
            if (-not (Add-InstalledToolToPath 'esptool' -PreferDiscovered)) { return $false }
        }
        'waveshare-esp32-s3-zero-debug' {
            if (-not (Add-InstalledToolToPath 'xtensa-esp32s3-elf-gcc')) { return $false }
            if (-not (Add-InstalledToolToPath 'esptool' -PreferDiscovered)) { return $false }
        }
    }

    return $true
}

function Show-Usage {
    Write-Host 'Usage: .\build.ps1 <target>'
    Write-Host ''
    Write-Host 'Deletes the build directory, configures the selected target, and builds it.'
    Write-Host ''
    Write-Host 'Available targets:'
    Write-Host '  host-debug    Debug build with tests and supported sanitizers'
    Write-Host '  host-release  Optimized release build with tests'
    Write-Host '  waveshare-rp2040-zero-debug  RP2040-Zero LED firmware'
    Write-Host '  raspberry-pi-pico-2-arm-debug  RP2350 Cortex-M33 compile target'
    Write-Host '  raspberry-pi-pico-2-riscv-debug  RP2350 Hazard3 RISC-V compile target'
    Write-Host '  waveshare-esp32-c6-zero-debug  ESP32-C6-Zero LED firmware'
    Write-Host '  waveshare-esp32-s3-zero-debug  ESP32-S3-Zero LED firmware'
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
    if (-not (Initialize-BuildTools $Target)) {
        exit 1
    }

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
