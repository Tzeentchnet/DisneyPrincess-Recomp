[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",

    [ValidateRange(1, 128)]
    [int]$Jobs = [Environment]::ProcessorCount
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Preset = if ($Configuration -eq "Release") { "pc-release" } else { "pc-debug" }

Push-Location $ProjectRoot
try {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE"
    }

    & cmake --build --preset $Preset -j $Jobs
    if ($LASTEXITCODE -ne 0) {
        throw "PC build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
