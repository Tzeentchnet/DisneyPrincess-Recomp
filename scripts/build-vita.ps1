[CmdletBinding()]
param(
    [ValidateRange(1, 128)]
    [int]$Jobs = 8,

    [ValidateRange(1, 256)]
    [int]$UnityBatchSize = 8
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildRoot = Join-Path $ProjectRoot "build\vita-release"
$VitaSdk = $env:VITASDK

if (-not $VitaSdk) {
    throw "VITASDK is not set. Install VitaSDK and set VITASDK to its root."
}

$Toolchain = Join-Path $VitaSdk "share\vita.toolchain.cmake"
if (-not (Test-Path -LiteralPath $Toolchain -PathType Leaf)) {
    throw "VitaSDK toolchain was not found: $Toolchain"
}

Push-Location $ProjectRoot
try {
    & cmake `
        -S $ProjectRoot `
        -B $BuildRoot `
        -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" `
        -DCMAKE_BUILD_TYPE=Release `
        "-DPS2X_RUNNER_UNITY_BUILD_BATCH_SIZE=$UnityBatchSize"
    if ($LASTEXITCODE -ne 0) {
        throw "Vita CMake configure failed with exit code $LASTEXITCODE"
    }

    & cmake --build $BuildRoot --target ps2EntryRunner.vpk -j $Jobs
    if ($LASTEXITCODE -ne 0) {
        throw "Vita build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
