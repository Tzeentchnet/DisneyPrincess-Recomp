[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",

    [string]$IsoPath
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildName = if ($Configuration -eq "Release") { "pc-release" } else { "pc-debug" }
$Runner = Join-Path $ProjectRoot "build\$BuildName\PS2Recomp\ps2xRuntime\DisneyPrincessRecomp.exe"
$BootElf = Join-Path $ProjectRoot ".work\disc\SLUS_216.60"
$PathMarker = Join-Path $ProjectRoot ".work\disc-image.path"

if (-not (Test-Path -LiteralPath $Runner -PathType Leaf)) {
    throw "PC runner does not exist: $Runner. Run scripts\build-pc.ps1 first."
}
if (-not (Test-Path -LiteralPath $BootElf -PathType Leaf)) {
    throw "Boot ELF does not exist: $BootElf. Run scripts\recompile.ps1 first."
}

if (-not $IsoPath) {
    if (-not (Test-Path -LiteralPath $PathMarker -PathType Leaf)) {
        throw "No ISO path was supplied and $PathMarker does not exist."
    }
    $IsoPath = [System.IO.File]::ReadAllText($PathMarker).Trim()
}

$ResolvedIso = (Resolve-Path -LiteralPath $IsoPath).Path
$PreviousImage = $env:DPRE_CD_IMAGE

Push-Location $ProjectRoot
try {
    $env:DPRE_CD_IMAGE = $ResolvedIso
    & $Runner $BootElf
    if ($LASTEXITCODE -ne 0) {
        throw "PC runner exited with code $LASTEXITCODE"
    }
}
finally {
    if ($null -eq $PreviousImage) {
        Remove-Item Env:DPRE_CD_IMAGE -ErrorAction SilentlyContinue
    }
    else {
        $env:DPRE_CD_IMAGE = $PreviousImage
    }
    Pop-Location
}
