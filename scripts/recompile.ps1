[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$IsoPath,

    [ValidateSet("boot", "all")]
    [string]$Extract = "all",

    [ValidateRange(1, 128)]
    [int]$Jobs = 8
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$FrameworkRoot = Join-Path $ProjectRoot "PS2Recomp"
$ToolBuild = Join-Path $FrameworkRoot "out\host-tools"
$WorkRoot = Join-Path $ProjectRoot ".work"
$DiscRoot = Join-Path $WorkRoot "disc"
$AnalysisRoot = Join-Path $WorkRoot "analysis"
$GeneratedRoot = Join-Path $WorkRoot "generated\game"
$LogRoot = Join-Path $WorkRoot "logs"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,

        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$Command' failed with exit code $LASTEXITCODE"
    }
}

New-Item -ItemType Directory -Force -Path $AnalysisRoot, $GeneratedRoot, $LogRoot | Out-Null

Push-Location $ProjectRoot
try {
    Invoke-Checked python ".\tools\prepare_disc.py" `
        --iso $IsoPath `
        --manifest ".work\disc-manifest.json" `
        --extract $Extract `
        --extract-root $DiscRoot

    $ResolvedIsoPath = (Resolve-Path -LiteralPath $IsoPath).Path
    $PathMarker = Join-Path $WorkRoot "disc-image.path"
    [System.IO.File]::WriteAllText(
        $PathMarker,
        $ResolvedIsoPath + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false)
    )

    Invoke-Checked cmake `
        -S $FrameworkRoot `
        -B $ToolBuild `
        -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        -DPS2X_BUILD_RUNTIME=OFF `
        -DPS2X_BUILD_TEST=OFF `
        -DPS2X_BUILD_STUDIO=OFF

    Invoke-Checked cmake --build $ToolBuild --target ps2_analyzer ps2_recomp -j $Jobs

    $Analyzer = Join-Path $ToolBuild "ps2xAnalyzer\ps2_analyzer.exe"
    $Recompiler = Join-Path $ToolBuild "ps2xRecomp\ps2_recomp.exe"
    $BootElf = Join-Path $DiscRoot "SLUS_216.60"
    $AnalyzerConfig = Join-Path $AnalysisRoot "analyzer.toml"
    $RecompilerConfig = Join-Path $AnalysisRoot "recomp.toml"

    & $Analyzer $BootElf $AnalyzerConfig *> (Join-Path $LogRoot "analyzer.log")
    if ($LASTEXITCODE -ne 0) {
        throw "PS2 ELF analyzer failed with exit code $LASTEXITCODE"
    }

    Invoke-Checked python ".\tools\configure_recomp.py" `
        --input $AnalyzerConfig `
        --output $RecompilerConfig `
        --generated-dir ".work\generated\game" `
        --workers $Jobs

    & $Recompiler $RecompilerConfig *> (Join-Path $LogRoot "recompiler.log")
    if ($LASTEXITCODE -ne 0) {
        throw "PS2 static recompiler failed with exit code $LASTEXITCODE"
    }

    Write-Host "Generated game code in $GeneratedRoot"
    Write-Host "Private disc-image path: $PathMarker"
    Write-Host "Analyzer log: $(Join-Path $LogRoot 'analyzer.log')"
    Write-Host "Recompiler log: $(Join-Path $LogRoot 'recompiler.log')"
}
finally {
    Pop-Location
}
