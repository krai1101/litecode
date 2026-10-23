param(
    [string]$QtRoot = (Join-Path $env:LOCALAPPDATA 'LiteCodeToolchain\Qt\6.11.2\msvc2022_64')
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -version '[17.0,18.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio 2022 C++ tools were not found.' }

Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64'

$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $ninja) {
    $ninja = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio') `
        -Filter ninja.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ninja) { $env:PATH = $ninja.DirectoryName + [IO.Path]::PathSeparator + $env:PATH }
}
if (-not $ninja) { throw 'Ninja was not found.' }

$analyzeRoot = 'build/analyze'
$analyzeCache = Join-Path $analyzeRoot 'CMakeCache.txt'
if (Test-Path -LiteralPath $analyzeCache -PathType Leaf) {
    cmake --build $analyzeRoot --target clean
    if ($LASTEXITCODE -ne 0) { throw 'Could not clean the previous static-analysis build.' }
}

cmake -S . -B $analyzeRoot --fresh -G Ninja -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=$QtRoot" -DBUILD_TESTING=OFF -DLITECODE_ENABLE_MSVC_ANALYZE=ON
if ($LASTEXITCODE -ne 0) { throw 'Static-analysis configuration failed.' }
cmake --build $analyzeRoot
if ($LASTEXITCODE -ne 0) { throw 'MSVC static analysis reported build failures.' }

Write-Host 'LiteCode MSVC static-analysis build passed.'
