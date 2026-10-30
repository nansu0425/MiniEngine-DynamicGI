param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# git repo 의 일부로 받는 asset. Root 는 upstream 의 Upstream 폴더를 받아 두는 곳이고, Paths 는 둘 모두에 대한 상대 경로다 ('' 는 폴더 전체).
$GitSources = @(
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
$CommitStampName = '.fetched-commit'

# zip 하나로 받는 asset. Sha256 이 받은 zip 과 다르면 아무것도 바꾸지 않고 실패한다. Root 에 zip 을 풀고, Paths 는 Root 에 대한 상대
# 경로다. Convert 는 다 받은 뒤에 Root 의 Source 를 asset 으로 바꾸는 Script 와 그 asset 을 여는 ModelViewer 의 Scene 이다. Script 는
# 이미 받았어도 실행하고, asset 을 Scene 이 가리키는 곳에 쓴다.
$Archives = @(
    @{
        # Benedikt Bitterli 의 Cornell Box 중 Mitsuba 3 판. CC0 이고 license 는 받은 cornell-box/LICENSE.txt 에 있다.
        Url = 'https://benedikt-bitterli.me/resources/mitsuba/cornell-box.zip'
        Sha256 = 'd57ffb9d176d34cc45d3d570b51e75be1dc6a4edc78879ba6fe40e2ce093e645'
        Root = 'MiniEngine/ModelViewer/Bitterli'
        Paths = @('cornell-box/LICENSE.txt')
        Convert = @{
            Script = 'scripts/cornellbox.py'
            Source = 'cornell-box/scene_v3.xml'
            Scene = 'MiniEngine/ModelViewer/Scenes/CornellBox.ini'
        }
    }
)

# Root 를 다 채운 뒤에만 쓰므로 받은 zip 의 sha256 과 함께 다 받았다는 것도 나타낸다.
$ArchiveStampName = '.fetched-sha256'

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

# zip 을 다 풀었는지 볼 Root 의 파일에는 Convert 가 읽는 Source 도 든다.
function Get-AbsentArchivePaths($Archive, $Folder) {
    return Get-AbsentPaths @{ Paths = @($Archive.Paths) + $Archive.Convert.Source } $Folder
}

function Test-ArchiveFetched($Archive, $Destination) {
    $Stamp = [System.IO.Path]::Combine($Destination, $ArchiveStampName)
    if (-not (Test-Path $Stamp) -or (Get-Content -Raw $Stamp) -ne $Archive.Sha256) { return $false }
    return (Get-AbsentArchivePaths $Archive $Destination).Count -eq 0
}

function Test-CommitFetched($Source, $Destination) {
    $Stamp = [System.IO.Path]::Combine($Destination, $CommitStampName)
    if (-not (Test-Path $Stamp) -or (Get-Content -Raw $Stamp) -ne $Source.Commit) { return $false }
    return (Get-AbsentPaths $Source $Destination).Count -eq 0
}

# 다 받고 stamp 를 쓴 Partial 을 Destination 과 바꾼다. 중간에 실패해도 기존 Destination 은 그대로이고 반쯤 찬 Destination 이 남지 않는다.
function Install-Fetched($Partial, $Destination, $Stamp) {
    if (Test-Path $Destination) {
        # stamp 를 먼저 지워, 지우다 멈춘 Root 를 다음 실행이 다 받은 것으로 보지 않게 한다.
        Remove-Item -Force -ErrorAction SilentlyContinue ([System.IO.Path]::Combine($Destination, $Stamp))
        Remove-Item -Recurse -Force $Destination
    }
    Move-Item $Partial $Destination
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

foreach ($Source in $GitSources) {
    $Destination = [System.IO.Path]::Combine($RepoRoot, $Source.Root)
    if (-not $Force -and (Test-CommitFetched $Source $Destination)) {
        Write-Host "$($Source.Root) already at $($Source.Commit). Use -Force to fetch again."
        continue
    }

    $TempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("minienginegi-assets-" + [guid]::NewGuid())
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
        Set-Content -NoNewline -Encoding ascii ([System.IO.Path]::Combine($Partial, $CommitStampName)) $Source.Commit
        Install-Fetched $Partial $Destination $CommitStampName
        Write-Host "Fetched $($Source.Root) at $($Source.Commit)"
    }
    finally {
        if (Test-Path $TempDir) { Remove-Item -Recurse -Force $TempDir }
        if (Test-Path $Partial) { Remove-Item -Recurse -Force $Partial }
    }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($Archive in $Archives) {
    $Destination = [System.IO.Path]::Combine($RepoRoot, $Archive.Root)
    if (-not $Force -and (Test-ArchiveFetched $Archive $Destination)) {
        Write-Host "$($Archive.Root) already has the zip of sha256 $($Archive.Sha256). Use -Force to fetch again."
    }
    else {
        $Zip = Join-Path ([System.IO.Path]::GetTempPath()) ("minienginegi-assets-" + [guid]::NewGuid() + ".zip")
        $Partial = "$Destination.partial"
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $Archive.Url -OutFile $Zip
            $Hash = (Get-FileHash -Algorithm SHA256 $Zip).Hash.ToLowerInvariant()
            if ($Hash -ne $Archive.Sha256) {
                throw "$($Archive.Url) has sha256 $Hash, not $($Archive.Sha256). $($Archive.Root) is left as it was"
            }

            if (Test-Path $Partial) { Remove-Item -Recurse -Force $Partial }
            # ExtractToDirectory 는 Partial 밖을 가리키는 항목이 있으면 실패한다.
            [System.IO.Compression.ZipFile]::ExtractToDirectory($Zip, $Partial)
            $Absent = Get-AbsentArchivePaths $Archive $Partial
            if ($Absent.Count) {
                throw "$($Archive.Url) has no $($Absent -join ', ')"
            }
            Set-Content -NoNewline -Encoding ascii ([System.IO.Path]::Combine($Partial, $ArchiveStampName)) $Archive.Sha256
            Install-Fetched $Partial $Destination $ArchiveStampName
            Write-Host "Fetched $($Archive.Root) from the zip of sha256 $($Archive.Sha256)"
        }
        finally {
            if (Test-Path $Zip) { Remove-Item -Force $Zip }
            if (Test-Path $Partial) { Remove-Item -Recurse -Force $Partial }
        }
    }

    # 받은 파일을 읽으므로 -I 로 cwd 와 사용자 site 를 sys.path 에서 뺀다.
    $Convert = $Archive.Convert
    python -I ([System.IO.Path]::Combine($RepoRoot, $Convert.Script)) ([System.IO.Path]::Combine($Destination, $Convert.Source)) `
        ([System.IO.Path]::Combine($RepoRoot, $Convert.Scene))
    if ($LASTEXITCODE -ne 0) { throw "$($Convert.Script) failed with exit code $LASTEXITCODE" }
}
