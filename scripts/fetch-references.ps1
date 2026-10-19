$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$ReferencesDir = Join-Path $RepoRoot 'references'
$Manifest = Get-Content -Raw -Encoding UTF8 (Join-Path $ReferencesDir 'manifest.json') | ConvertFrom-Json

function Invoke-Git {
    git @args
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed with exit code $LASTEXITCODE" }
}

function Test-GitCommit($Dir, $Commit) {
    git -C $Dir cat-file -e "$Commit^{commit}" 2>$null
    return $LASTEXITCODE -eq 0
}

foreach ($Reference in $Manifest) {
    $Destination = Join-Path $ReferencesDir $Reference.name

    if (-not (Test-Path $Destination)) {
        Invoke-Git clone --quiet --filter=blob:none --no-checkout $Reference.url $Destination
    }
    elseif ((git -C $Destination rev-parse HEAD) -eq $Reference.commit) {
        Write-Host "$($Reference.name) already at $($Reference.commit)"
        continue
    }
    elseif (git -C $Destination status --porcelain) {
        # reference 안에서 실험한 변경을 덮어쓰지 않는다
        Write-Warning "$($Reference.name) has local changes. Skipped."
        continue
    }

    if (-not (Test-GitCommit $Destination $Reference.commit)) {
        Invoke-Git -C $Destination fetch --quiet --filter=blob:none origin $Reference.commit
    }
    Invoke-Git -C $Destination checkout --quiet --detach $Reference.commit
    Invoke-Git -C $Destination submodule update --quiet --init --recursive
    Write-Host "Fetched $($Reference.name) at $($Reference.commit)"
}
