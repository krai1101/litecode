param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$QtRoot = (Join-Path $env:LOCALAPPDATA 'LiteCodeToolchain\Qt\6.11.2\msvc2022_64'),

    [string]$Target = '',

    [ValidateRange(1, 32)]
    [int]$ParallelJobs = 2,

    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

$vsPath = & $vswhere -latest -products * -version '[17.0,18.0)' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw 'Visual Studio 2022 with the MSVC x64 C++ tools was not found.'
}

$devShellModule = Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShellModule
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
if (-not $ninja) {
    throw 'Ninja was not found. Install it or add ninja.exe to PATH.'
}

if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw "Qt 6 was not found at '$QtRoot'. Pass -QtRoot with the MSVC 2022 Qt directory."
}
$env:PATH = (Join-Path $QtRoot 'bin') + [IO.Path]::PathSeparator + $env:PATH

$preset = $Configuration.ToLowerInvariant()
$buildTesting = if ($SkipTests) { 'OFF' } else { 'ON' }
cmake --preset $preset "-DCMAKE_PREFIX_PATH=$QtRoot" "-DBUILD_TESTING=$buildTesting"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

if ($Target) {
    cmake --build --preset $preset --target $Target --parallel $ParallelJobs
} else {
    cmake --build --preset $preset --parallel $ParallelJobs
}
if ($LASTEXITCODE -ne 0) { throw 'The build failed.' }

if (-not $SkipTests) {
    $testResultsDirectory = Join-Path $repositoryRoot "build\$preset\tests"
    if (Test-Path -LiteralPath $testResultsDirectory) {
        Get-ChildItem -LiteralPath $testResultsDirectory -Filter '*.qtest.txt' -File |
            Remove-Item -Force
    }
    ctest --preset $preset --output-on-failure --parallel $ParallelJobs
    if ($LASTEXITCODE -ne 0) {
        $lastTestLog = Join-Path $repositoryRoot "build\$preset\Testing\Temporary\LastTest.log"
        if (Test-Path -LiteralPath $lastTestLog) {
            Write-Host 'CTest failure details:'
            Get-Content -LiteralPath $lastTestLog
        }
        Get-ChildItem -LiteralPath $testResultsDirectory -Filter '*.qtest.txt' -ErrorAction SilentlyContinue |
            ForEach-Object {
                Write-Host "Qt test failure details ($($_.Name)):"
                Get-Content -LiteralPath $_.FullName
            }
        throw 'Tests failed.'
    }
}

if ($SkipTests) {
    Write-Host "LiteCode $Configuration target build passed."
} else {
    Write-Host "LiteCode $Configuration build and tests passed."
}
