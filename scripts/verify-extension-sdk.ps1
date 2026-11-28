param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot,

    [string]$BuildRoot = '',

    [string]$QtRoot = (Join-Path $env:LOCALAPPDATA 'LiteCodeToolchain\Qt\6.11.2\msvc2022_64')
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$packagePath = (Resolve-Path -LiteralPath $PackageRoot).Path
if (-not $BuildRoot) {
    $BuildRoot = Join-Path $repositoryRoot 'build\extension-sdk-verification'
}
$fixturePath = Join-Path $repositoryRoot 'tests\fixtures\installed-sdk'
$sdkConfig = Join-Path $packagePath 'sdk\lib\cmake\LiteCodeExtensionSDK'
if (-not (Test-Path -LiteralPath (Join-Path $sdkConfig 'LiteCodeExtensionSDKConfig.cmake'))) {
    throw "The installed SDK CMake package is missing from '$packagePath'."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}
$vsPath = & $vswhere -latest -products * -version '[17.0,18.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw 'Visual Studio 2022 with the MSVC x64 C++ tools was not found.'
}
Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64'

$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $ninja) {
    $visualStudioRoot = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio'
    $ninja = Get-ChildItem -LiteralPath $visualStudioRoot -Filter ninja.exe -Recurse `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ninja) {
        $env:PATH = $ninja.DirectoryName + [IO.Path]::PathSeparator + $env:PATH
    }
}
if (-not $ninja) { throw 'Ninja was not found.' }

cmake -S $fixturePath -B $BuildRoot -G Ninja `
    "-DCMAKE_PREFIX_PATH=$QtRoot" "-DLiteCodeExtensionSDK_DIR=$sdkConfig" `
    '-DCMAKE_BUILD_TYPE=Release'
if ($LASTEXITCODE -ne 0) { throw 'SDK fixture configuration failed.' }
cmake --build $BuildRoot --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'SDK fixture build failed.' }
Write-Host 'Extension fixture built independently against the installed SDK.'
