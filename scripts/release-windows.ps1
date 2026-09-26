param(
    [string]$QtRoot = (Join-Path $env:LOCALAPPDATA 'LiteCodeToolchain\Qt\6.11.2\msvc2022_64'),

    [ValidateRange(1, 32)]
    [int]$ParallelJobs = 2,

    [switch]$SkipAnalyze
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$releaseRoot = Join-Path $root 'build\release'
$zipPath = Join-Path $releaseRoot 'LiteCode-0.1.0-win64.zip'

& (Join-Path $PSScriptRoot 'build-windows.ps1') -Configuration Release `
    -QtRoot $QtRoot -ParallelJobs $ParallelJobs
if ($LASTEXITCODE -ne 0) { throw 'Release build or CTest failed.' }

& (Join-Path $PSScriptRoot 'format.ps1') -Check
if ($LASTEXITCODE -ne 0) { throw 'Formatting check failed.' }

& (Join-Path $PSScriptRoot 'check-architecture.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Architecture check failed.' }

if (-not $SkipAnalyze) {
    & (Join-Path $PSScriptRoot 'analyze-windows.ps1') -QtRoot $QtRoot
    if ($LASTEXITCODE -ne 0) { throw 'MSVC static analysis failed.' }
}

cmake --build $releaseRoot --target package --parallel $ParallelJobs
if ($LASTEXITCODE -ne 0) { throw 'CPack failed.' }
if (-not (Test-Path -LiteralPath $zipPath -PathType Leaf)) {
    throw "Expected package was not produced at '$zipPath'."
}

$smokeRoot = Join-Path $releaseRoot ('package-smoke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $smokeRoot | Out-Null
try {
    Expand-Archive -LiteralPath $zipPath -DestinationPath $smokeRoot
    $packageRoot = Get-ChildItem -LiteralPath $smokeRoot -Directory | Select-Object -First 1
    if ($null -eq $packageRoot) { throw 'The package did not contain a root directory.' }

    & (Join-Path $PSScriptRoot 'verify-package.ps1') -PackageRoot $packageRoot.FullName
    if ($LASTEXITCODE -ne 0) { throw 'Package contents verification failed.' }

    $executables = @(Get-ChildItem -LiteralPath $packageRoot.FullName -Recurse -File `
        -Filter LiteCode.exe)
    if ($executables.Count -ne 1) {
        throw "Expected exactly one packaged LiteCode.exe, found $($executables.Count)."
    }

    $savedPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $process = Start-Process -FilePath $executables[0].FullName `
            -WorkingDirectory $executables[0].DirectoryName -WindowStyle Hidden -PassThru
        try {
            if (-not $process.WaitForInputIdle(15000)) {
                throw 'Packaged LiteCode did not reach Windows input-idle within 15 seconds.'
            }
            Start-Sleep -Milliseconds 500
            $process.Refresh()
            if ($process.HasExited -or -not $process.Responding) {
                throw 'Packaged LiteCode did not remain responsive.'
            }
        } finally {
            if ($null -ne $process -and -not $process.HasExited) {
                Stop-Process -Id $process.Id
                $process.WaitForExit(5000) | Out-Null
            }
        }
    } finally {
        $env:PATH = $savedPath
    }
} finally {
    $resolvedRelease = (Resolve-Path -LiteralPath $releaseRoot).Path.TrimEnd('\') + '\'
    $resolvedSmoke = (Resolve-Path -LiteralPath $smokeRoot).Path
    if (-not $resolvedSmoke.StartsWith($resolvedRelease,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected smoke directory '$resolvedSmoke'."
    }
    $cleanupDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        try {
            Remove-Item -LiteralPath $resolvedSmoke -Recurse -Force -ErrorAction Stop
            break
        } catch {
            if ([DateTime]::UtcNow -ge $cleanupDeadline) { throw }
            Start-Sleep -Milliseconds 250
        }
    } while ($true)
}

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
Write-Host "LiteCode Windows release gate passed."
Write-Host "Package: $zipPath"
Write-Host "SHA-256: $hash"
