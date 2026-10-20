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

function Set-SparseCheckout($Dir, $Patterns) {
    if ($Patterns) {
        # cone mode 는 하위 디렉토리를 제외할 수 없어서 non-cone pattern 을 쓴다
        Invoke-Git -C $Dir sparse-checkout set --no-cone @Patterns
    }
    elseif ((git -C $Dir config --get core.sparseCheckout) -eq 'true') {
        Invoke-Git -C $Dir sparse-checkout disable
    }
}

function Get-FileReference($Reference, $Destination) {
    if ((Test-Path $Destination) -and (Get-FileHash -Algorithm SHA256 $Destination).Hash -eq $Reference.sha256) {
        Write-Host "$($Reference.name) already matches sha256"
        return
    }

    # 받다가 실패하거나 hash 가 다르면 기존 파일을 건드리지 않도록 임시 파일에 받는다
    $Download = "$Destination.download"
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -UseBasicParsing -Uri $Reference.url -OutFile $Download
    $Hash = (Get-FileHash -Algorithm SHA256 $Download).Hash
    if ($Hash -ne $Reference.sha256) {
        Remove-Item $Download
        throw "$($Reference.name) sha256 mismatch: expected $($Reference.sha256), got $Hash"
    }
    Move-Item -Force $Download $Destination
    Write-Host "Fetched $($Reference.name)"
}

foreach ($Reference in $Manifest) {
    $Destination = Join-Path $ReferencesDir $Reference.name

    if ($Reference.sha256) {
        Get-FileReference $Reference $Destination
        continue
    }

    $IsNewClone = -not (Test-Path $Destination)

    if ($IsNewClone) {
        Invoke-Git clone --quiet --filter=blob:none --no-checkout $Reference.url $Destination
    }
    elseif (git -C $Destination status --porcelain) {
        # reference 안에서 실험한 변경을 덮어쓰지 않는다
        Write-Warning "$($Reference.name) has local changes. Skipped."
        continue
    }

    # checkout 전에 적용해야 sparse 범위 밖의 blob 을 받지 않는다
    Set-SparseCheckout $Destination $Reference.sparse

    # --no-checkout clone 의 HEAD 는 manifest commit 과 같아도 working tree 가 비어 있다
    if (-not $IsNewClone -and (git -C $Destination rev-parse HEAD) -eq $Reference.commit) {
        Write-Host "$($Reference.name) already at $($Reference.commit)"
        continue
    }

    if (-not (Test-GitCommit $Destination $Reference.commit)) {
        Invoke-Git -C $Destination fetch --quiet --filter=blob:none origin $Reference.commit
    }
    Invoke-Git -C $Destination checkout --quiet --detach $Reference.commit
    # sparse-checkout 은 submodule 에 적용되지 않아서 manifest 에 적은 submodule 만 받는다
    if ($Reference.submodules) {
        Invoke-Git -C $Destination submodule update --quiet --init --recursive -- @($Reference.submodules)
    }
    Write-Host "Fetched $($Reference.name) at $($Reference.commit)"
}
