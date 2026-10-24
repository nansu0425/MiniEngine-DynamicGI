param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# Root 는 upstream 의 Upstream 폴더를 받아 두는 곳이고, Paths 는 둘 모두에 대한 상대 경로다 ('' 는 폴더 전체).
$Sources = @(
    @{
        Url = 'https://github.com/microsoft/DirectX-Graphics-Samples.git'
        Commit = 'e5975f9b0744fc5096dd593ed72bf8f5c004164f'
        Upstream = 'MiniEngine/ModelViewer/Textures'
        Root = 'MiniEngine/ModelViewer/Textures'
        Paths = @('')
    },
    @{
        Url = 'https://github.com/KhronosGroup/glTF-Sample-Assets.git'
        Commit = '8f492390ddc7a2ae221a1369ef560589cf8c70fb'
        # Models/Sponza/LICENSE.md 의 상대 link 가 맞도록 Root 아래에 upstream 경로를 그대로 둔다.
        Upstream = ''
        Root = 'MiniEngine/ModelViewer/glTF-Sample-Assets'
        Paths = @(
            'Models/Sponza/glTF/',
            'Models/Sponza/LICENSE.md',
            'Models/Sponza/README.md',
            'LICENSES/LicenseRef-CRYENGINE-Agreement.txt'
        )
    }
)

# Root 를 다 채운 뒤에만 쓰므로 받은 commit 과 함께 다 받았다는 것도 나타낸다.
$StampName = '.fetched-commit'

$RepoRoot = Split-Path -Parent $PSScriptRoot

function Invoke-Git {
    git @args
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed with exit code $LASTEXITCODE" }
}

function Get-UpstreamPath($Source, $Path) {
    return (@($Source.Upstream, $Path) | Where-Object { $_ }) -join '/'
}

function Get-AbsentPaths($Source, $Folder) {
    return @($Source.Paths | Where-Object { -not (Test-Path ([System.IO.Path]::Combine($Folder, $_))) })
}

function Test-Fetched($Source, $Destination) {
    $Stamp = [System.IO.Path]::Combine($Destination, $StampName)
    if (-not (Test-Path $Stamp) -or (Get-Content -Raw $Stamp) -ne $Source.Commit) { return $false }
    return (Get-AbsentPaths $Source $Destination).Count -eq 0
}

function Copy-Asset($From, $To) {
    if (Test-Path -PathType Leaf $From) {
        New-Item -ItemType Directory -Force (Split-Path -Parent $To) | Out-Null
        Copy-Item $From $To
    }
    else {
        New-Item -ItemType Directory -Force $To | Out-Null
        Get-ChildItem -Force $From | Copy-Item -Recurse -Force -Destination $To
    }
}

foreach ($Source in $Sources) {
    $Destination = [System.IO.Path]::Combine($RepoRoot, $Source.Root)
    if (-not $Force -and (Test-Fetched $Source $Destination)) {
        Write-Host "$($Source.Root) already at $($Source.Commit). Use -Force to fetch again."
        continue
    }

    $TempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("minienginegi-assets-" + [guid]::NewGuid())
    # 다 복사하고 stamp 를 쓴 뒤에 Root 와 바꾼다. 중간에 실패해도 기존 Root 는 그대로이고 반쯤 찬 Root 가 남지 않는다.
    $Partial = "$Destination.partial"
    try {
        Invoke-Git init --quiet $TempDir
        Invoke-Git -C $TempDir remote add origin $Source.Url
        Invoke-Git -C $TempDir sparse-checkout set --no-cone @($Source.Paths | ForEach-Object { '/' + (Get-UpstreamPath $Source $_) })
        Invoke-Git -C $TempDir fetch --quiet --depth 1 --filter=blob:none origin $Source.Commit
        # 받은 text 파일이 upstream 의 byte 그대로이도록 사용자의 autocrlf 를 따르지 않는다.
        Invoke-Git -C $TempDir -c core.autocrlf=false checkout --quiet FETCH_HEAD

        $Upstream = [System.IO.Path]::Combine($TempDir, $Source.Upstream)
        $Absent = Get-AbsentPaths $Source $Upstream
        if ($Absent.Count) {
            throw "$($Source.Url) at $($Source.Commit) has no $(($Absent | ForEach-Object { Get-UpstreamPath $Source $_ }) -join ', ')"
        }

        if (Test-Path $Partial) { Remove-Item -Recurse -Force $Partial }
        foreach ($Path in $Source.Paths) {
            Copy-Asset ([System.IO.Path]::Combine($Upstream, $Path)) ([System.IO.Path]::Combine($Partial, $Path))
        }
        Set-Content -NoNewline -Encoding ascii ([System.IO.Path]::Combine($Partial, $StampName)) $Source.Commit

        if (Test-Path $Destination) {
            # stamp 를 먼저 지워, 지우다 멈춘 Root 를 다음 실행이 다 받은 것으로 보지 않게 한다.
            Remove-Item -Force -ErrorAction SilentlyContinue ([System.IO.Path]::Combine($Destination, $StampName))
            Remove-Item -Recurse -Force $Destination
        }
        Move-Item $Partial $Destination
        Write-Host "Fetched $($Source.Root) at $($Source.Commit)"
    }
    finally {
        if (Test-Path $TempDir) { Remove-Item -Recurse -Force $TempDir }
        if (Test-Path $Partial) { Remove-Item -Recurse -Force $Partial }
    }
}
