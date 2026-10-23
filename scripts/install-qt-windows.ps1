param(
    [ValidateSet('6.11.2')]
    [string]$Version = '6.11.2',

    [string]$QtRoot = (Join-Path $env:LOCALAPPDATA "LiteCodeToolchain\Qt\$Version\msvc2022_64"),

    [string]$DownloadRoot = (Join-Path $env:LOCALAPPDATA "LiteCodeToolchain\QtDownloads\$Version-msvc2022_64")
)

$ErrorActionPreference = 'Stop'

function Export-QtEnvironment {
    param([string]$Root)

    if ($env:GITHUB_ENV) {
        Add-Content -LiteralPath $env:GITHUB_ENV -Value "QT_ROOT_DIR=$Root"
        Add-Content -LiteralPath $env:GITHUB_ENV -Value "CMAKE_PREFIX_PATH=$Root"
    }
    if ($env:GITHUB_PATH) {
        Add-Content -LiteralPath $env:GITHUB_PATH -Value (Join-Path $Root 'bin')
    }
}

$qtConfig = Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'
$qmake = Join-Path $QtRoot 'bin\qmake.exe'
$requiredFiles = @(
    $qtConfig,
    (Join-Path $QtRoot 'lib\cmake\Qt6Core5Compat\Qt6Core5CompatConfig.cmake'),
    (Join-Path $QtRoot 'lib\cmake\Qt6Svg\Qt6SvgConfig.cmake'),
    (Join-Path $QtRoot 'bin\windeployqt.exe'),
    $qmake
)
$installationComplete = @($requiredFiles | Where-Object {
        -not (Test-Path -LiteralPath $_ -PathType Leaf)
    }).Count -eq 0
if ($installationComplete) {
    $installedVersion = (& $qmake -query QT_VERSION).Trim()
    if ($installedVersion -ne $Version) {
        throw "Qt $installedVersion, rather than Qt $Version, is installed at '$QtRoot'."
    }
    Export-QtEnvironment -Root $QtRoot
    Write-Host "Qt $Version is already installed at '$QtRoot'."
    return
}
if ((Test-Path -LiteralPath $QtRoot) -and
    (Get-ChildItem -LiteralPath $QtRoot -Force -ErrorAction SilentlyContinue)) {
    throw "Refusing to merge into the incomplete Qt installation at '$QtRoot'."
}

$compactVersion = $Version.Replace('.', '')
$repository = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/" +
    "qt6_$compactVersion/qt6_${compactVersion}_msvc2022_64"
$metadataUrl = "$repository/Updates.xml"
Write-Host "Reading public Qt repository metadata from $metadataUrl"
[xml]$metadata = (Invoke-WebRequest -UseBasicParsing -Uri $metadataUrl).Content

$basePackageName = "qt.qt6.$compactVersion.win64_msvc2022_64"
$compatPackageName = "qt.qt6.$compactVersion.addons.qt5compat.win64_msvc2022_64"
$basePackage = $metadata.Updates.PackageUpdate | Where-Object Name -EQ $basePackageName
$compatPackage = $metadata.Updates.PackageUpdate | Where-Object Name -EQ $compatPackageName
if ($null -eq $basePackage -or $null -eq $compatPackage) {
    throw "Qt $Version MSVC 2022 packages were not found in the public repository."
}

$requests = @(
    [pscustomobject]@{
        Package = $basePackage
        Prefix = 'qtbase-'
        Sha256 = 'FD984B7264361B4DD3FD2A417702CA1258E4086268F2EE6A69B9A393D9C3F6BB'
    },
    [pscustomobject]@{
        Package = $basePackage
        Prefix = 'qtsvg-'
        Sha256 = '417F44499C835B2303F3FF78043179BB23442F33D5D2816FBF7E8DBF275B1C89'
    },
    [pscustomobject]@{
        Package = $compatPackage
        Prefix = 'qt5compat-'
        Sha256 = '132B38E3E8F4316E1A5694D10C40A4B1CD6B3A57C8AC375FD02E98020092C7BE'
    }
)
New-Item -ItemType Directory -Path $QtRoot, $DownloadRoot -Force | Out-Null

$sevenZipCommand = Get-Command 7z -ErrorAction SilentlyContinue
$sevenZip = if ($sevenZipCommand) { $sevenZipCommand.Source } else { $null }
if (-not $sevenZip) {
    $installedSevenZip = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
    if (Test-Path -LiteralPath $installedSevenZip -PathType Leaf) {
        $sevenZip = $installedSevenZip
    }
}
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $sevenZip -and -not $python) {
    throw 'Neither 7-Zip nor Python with py7zr is available to extract Qt archives.'
}

foreach ($request in $requests) {
    $archives = @($request.Package.DownloadableArchives -split ',' |
            ForEach-Object { $_.Trim() } | Where-Object { $_.StartsWith($request.Prefix) })
    if ($archives.Count -ne 1) {
        throw "Expected one $($request.Prefix) archive, found $($archives.Count)."
    }

    $archiveName = $archives[0]
    $remoteName = $request.Package.Version + $archiveName
    $packageUrl = "$repository/$($request.Package.Name)/$remoteName"
    $archivePath = Join-Path $DownloadRoot $remoteName

    Write-Host "Downloading $archiveName"
    Invoke-WebRequest -UseBasicParsing -Uri $packageUrl -OutFile $archivePath
    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if ($actualHash -ne $request.Sha256) {
        throw "The repository-pinned SHA-256 did not match $archiveName."
    }

    if ($sevenZip) {
        & $sevenZip x -y "-o$QtRoot" $archivePath | Out-Host
    } else {
        & $python.Source -m py7zr t $archivePath
        if ($LASTEXITCODE -eq 0) {
            & $python.Source -m py7zr x $archivePath $QtRoot
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Extraction failed for $archiveName."
    }
}

foreach ($requiredFile in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "The installed Qt package is incomplete: '$requiredFile' is missing."
    }
}
$installedVersion = (& $qmake -query QT_VERSION).Trim()
if ($installedVersion -ne $Version) {
    throw "The installed qmake reported Qt $installedVersion instead of Qt $Version."
}

Export-QtEnvironment -Root $QtRoot
Write-Host "Qt $Version was installed from the public Qt repository at '$QtRoot'."
