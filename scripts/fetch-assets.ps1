param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$UpstreamUrl = 'https://github.com/microsoft/DirectX-Graphics-Samples.git'
$UpstreamCommit = 'e5975f9b0744fc5096dd593ed72bf8f5c004164f'
$AssetDirs = @(
    'MiniEngine/ModelViewer/Sponza',
    'MiniEngine/ModelViewer/Textures'
)

$RepoRoot = Split-Path -Parent $PSScriptRoot

function Invoke-Git {
    git @args
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed with exit code $LASTEXITCODE" }
}

$Missing = $AssetDirs | Where-Object { $Force -or -not (Test-Path (Join-Path $RepoRoot $_)) }
if (-not $Missing) {
    Write-Host 'Assets already present. Use -Force to fetch again.'
    return
}

$TempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("minienginegi-assets-" + [guid]::NewGuid())
try {
    Invoke-Git init --quiet $TempDir
    Invoke-Git -C $TempDir remote add origin $UpstreamUrl
    Invoke-Git -C $TempDir sparse-checkout set --no-cone @($Missing | ForEach-Object { "/$_/" })
    Invoke-Git -C $TempDir fetch --quiet --depth 1 --filter=blob:none origin $UpstreamCommit
    Invoke-Git -C $TempDir checkout --quiet FETCH_HEAD

    foreach ($Dir in $Missing) {
        $Destination = Join-Path $RepoRoot $Dir
        if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
        Copy-Item -Recurse (Join-Path $TempDir $Dir) $Destination
        Write-Host "Fetched $Dir"
    }
}
finally {
    if (Test-Path $TempDir) { Remove-Item -Recurse -Force $TempDir }
}
