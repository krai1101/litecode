param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'
$resolvedRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$required = @(
    'bin\LiteCode.exe',
    'bin\LiteCodeExtensionHost.exe',
    'bin\LiteCodeGitAskPass.exe',
    'bin\rg.exe',
    'extensions\python-debugger\litecode_python_debugger_extension.dll',
    'extensions\python-debugger\bundled\libs\debugpy\adapter\__main__.py',
    'extensions\codex\litecode_codex_extension.dll',
    'extensions\codex\runtime\bin\codex.exe',
    'extensions\codex\runtime\bin\codex-code-mode-host.exe',
    'extensions\codex\runtime\codex-resources\codex-command-runner.exe',
    'extensions\codex\runtime\codex-resources\codex-windows-sandbox-setup.exe',
    'extensions\codex\runtime\codex-package.json',
    'bin\Qt6Core.dll',
    'bin\Qt6Network.dll',
    'bin\Qt6Core5Compat.dll',
    'bin\Qt6Gui.dll',
    'bin\Qt6Svg.dll',
    'bin\Qt6Widgets.dll',
    'plugins\platforms\qwindows.dll',
    'LICENSE',
    'NOTICE.md',
    'THIRD_PARTY.md',
    'SBOM.spdx.json',
    'sdk\include\extensions\ExtensionApi.h',
    'sdk\lib\litecode_extension_sdk.lib',
    'sdk\lib\cmake\LiteCodeExtensionSDK\LiteCodeExtensionSDKConfig.cmake',
    'sdk\lib\cmake\LiteCodeExtensionSDK\LiteCodeExtensionSDKConfigVersion.cmake',
    'sdk\lib\cmake\LiteCodeExtensionSDK\LiteCodeExtensionSDKTargets.cmake',
    'licenses\LGPL-3.0.txt',
    'licenses\GPL-3.0.txt',
    'licenses\SCINTILLA.txt',
    'licenses\LIBVTERM-MIT.txt',
    'licenses\CODICONS-CC-BY-4.0.txt',
    'licenses\SETI-UI-MIT.txt',
    'licenses\VSCODE-MIT.txt',
    'licenses\LUCIDE-ISC.txt',
    'licenses\RIPGREP-MIT.txt',
    'licenses\CODEX-APACHE-2.0.txt',
    'licenses\CODEX-NOTICE.txt'
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
    'ripgrep',
    'OpenAI Codex'
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
