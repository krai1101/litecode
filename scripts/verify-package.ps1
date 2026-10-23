param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'
$resolvedRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$required = @(
    'bin\LiteCode.exe',
    'bin\rg.exe',
    'bin\Qt6Core.dll',
    'bin\Qt6Core5Compat.dll',
    'bin\Qt6Gui.dll',
    'bin\Qt6Svg.dll',
    'bin\Qt6Widgets.dll',
    'plugins\platforms\qwindows.dll',
    'LICENSE',
    'NOTICE.md',
    'THIRD_PARTY.md',
    'SBOM.spdx.json',
    'licenses\LGPL-3.0.txt',
    'licenses\GPL-3.0.txt',
    'licenses\SCINTILLA.txt',
    'licenses\LIBVTERM-MIT.txt',
    'licenses\CODICONS-CC-BY-4.0.txt',
    'licenses\SETI-UI-MIT.txt',
    'licenses\VSCODE-MIT.txt',
    'licenses\LUCIDE-ISC.txt',
    'licenses\RIPGREP-MIT.txt'
)

$missing = @($required | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $resolvedRoot $_) -PathType Leaf)
})
if ($missing.Count -gt 0) {
    throw "Package is missing required files: $($missing -join ', ')"
}

$sbomPath = Join-Path $resolvedRoot 'SBOM.spdx.json'
try {
    $sbom = Get-Content -LiteralPath $sbomPath -Raw | ConvertFrom-Json
} catch {
    throw "Package SBOM is not valid JSON: $($_.Exception.Message)"
}
$requiredPackages = @(
    'LiteCode',
    'Qt',
    'Scintilla',
    'Lexilla',
    'libvterm',
    'Visual Studio Code Codicons',
    'Visual Studio Code Seti icon theme / Seti UI',
    'Visual Studio Code Light Modern and Dark Modern themes',
    'Lucide Icons',
    'ripgrep'
)
$sbomPackageNames = @($sbom.packages | ForEach-Object name)
$missingPackages = @($requiredPackages | Where-Object { $sbomPackageNames -notcontains $_ })
if ($missingPackages.Count -gt 0) {
    throw "Package SBOM is missing shipped components: $($missingPackages -join ', ')"
}

$executables = @(Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Filter LiteCode.exe)
if ($executables.Count -ne 1) {
    throw "Expected exactly one LiteCode.exe in the package, found $($executables.Count)."
}

Write-Host "LiteCode package contents passed verification at '$resolvedRoot'."
