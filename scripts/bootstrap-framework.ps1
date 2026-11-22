[CmdletBinding()]
param(
    [string]$FrameworkRoot,

    [string]$Branch = "game/disney-princess-enchanted-journey"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $FrameworkRoot) {
    $FrameworkRoot = Join-Path $ProjectRoot "PS2Recomp"
}

$UpstreamRepository = "https://github.com/ran-j/PS2Recomp.git"
$Repository = $UpstreamRepository
$SubmoduleConfig = Join-Path $ProjectRoot ".gitmodules"
if (Test-Path -LiteralPath $SubmoduleConfig) {
    $ConfiguredRepository = & git config --file $SubmoduleConfig `
        --get submodule.PS2Recomp.url
    if ($LASTEXITCODE -eq 0 -and $ConfiguredRepository) {
        $Repository = $ConfiguredRepository
    }
    elseif ($LASTEXITCODE -ne 1) {
        throw "Unable to read the PS2Recomp submodule URL"
    }
}

$BaseCommit = "e5636981a1c9de368f6b71e819c3dc188dfedbf4"
$PullRequests = 352, 385, 368, 371, 399, 400, 415, 443
$Commits = @(
    "5be9ada87973e3388229af719640d386cbf49e42",
    "04a35083cca3daba4c2f85f7134989866fb14568",
    "ccbf6a08634088b1842c3a0d3f17bd710510f735",
    "1883a2d5476ca4015363003a205e812484f7851d",
    "55005e9432043eafcd0529859005dd4efdd9ab31",
    "a27e647ca28b0ae8937135d5a87a906534b7d034",
    "92a8ad43810737c099e84ef020dcabfc77b01e57",
    "1acce178258c066431407fbe62a88cb26de343a1",
    "649956f622faf8c84d3e18ea100141a9e64c40b0",
    "9a73ddc51a58cccaf15f394920cc59c7127c9e4c",
    "b6d2d81627cede31673d4302aa96d1aac03a19fc"
)

function Invoke-Git {
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & git -C $FrameworkRoot @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path -LiteralPath $FrameworkRoot)) {
    & git clone $Repository $FrameworkRoot
    if ($LASTEXITCODE -ne 0) {
        throw "git clone failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $FrameworkRoot ".git"))) {
    throw "Framework path is not a Git checkout: $FrameworkRoot"
}

$Remotes = @(& git -C $FrameworkRoot remote)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to inspect the PS2Recomp remotes"
}
if ($Remotes -notcontains "upstream") {
    Invoke-Git remote add upstream $UpstreamRepository
}
else {
    $UpstreamUrl = & git -C $FrameworkRoot remote get-url upstream
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect the PS2Recomp upstream remote"
    }
    if ($UpstreamUrl -ne $UpstreamRepository) {
        throw "Unexpected PS2Recomp upstream remote: $UpstreamUrl"
    }
}

$Status = & git -C $FrameworkRoot status --porcelain
if ($LASTEXITCODE -ne 0) {
    throw "Unable to inspect the PS2Recomp checkout"
}
if ($Status) {
    throw "PS2Recomp has uncommitted changes. Commit or stash them before bootstrapping."
}

Invoke-Git fetch upstream $BaseCommit
foreach ($PullRequest in $PullRequests) {
    Invoke-Git fetch upstream `
        "refs/pull/$PullRequest/head:refs/remotes/upstream/pr/$PullRequest"
}

& git -C $FrameworkRoot show-ref --verify --quiet "refs/heads/$Branch"
if ($LASTEXITCODE -eq 0) {
    Invoke-Git switch $Branch
}
elseif ($LASTEXITCODE -eq 1) {
    Invoke-Git switch --create $Branch $BaseCommit
}
else {
    throw "Unable to inspect branch $Branch"
}

foreach ($Commit in $Commits) {
    $Cherry = & git -C $FrameworkRoot cherry HEAD $Commit
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to compare commit $Commit against HEAD"
    }

    if ($Cherry -match "^-") {
        Write-Host "Already applied: $Commit"
        continue
    }

    Invoke-Git cherry-pick $Commit
}

$Head = & git -C $FrameworkRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) {
    throw "Unable to read the resulting PS2Recomp revision"
}

Write-Host "PS2Recomp is ready on $Branch at $Head"
