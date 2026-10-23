param([switch]$Check)

$formatter = Get-Command clang-format -ErrorAction SilentlyContinue
if (-not $formatter) {
    $bundled = Join-Path $env:LOCALAPPDATA `
        'LiteCodeToolchain\clang-format-20\clang_format\data\bin\clang-format.exe'
    if (Test-Path -LiteralPath $bundled) { $formatter = Get-Item $bundled }
}
if (-not $formatter) {
    throw 'clang-format 20+ was not found. Install the Apache-2.0 licensed LLVM formatter.'
}

# Get-Command returns an ApplicationInfo object on GitHub's PowerShell runner, while the
# bundled fallback is a FileInfo object. Both expose a usable executable path, but neither
# guarantees that FullName is populated on the command object.
$formatterPath = if ($formatter -is [System.IO.FileInfo]) {
    $formatter.FullName
} elseif (-not [string]::IsNullOrWhiteSpace($formatter.Source)) {
    $formatter.Source
} else {
    $formatter.Path
}
if ([string]::IsNullOrWhiteSpace($formatterPath)) {
    throw 'clang-format was found but its executable path could not be resolved.'
}

$files = @(Get-ChildItem src,tests,benchmarks -Recurse -File -Include *.cpp,*.h |
    Where-Object { $_.FullName -notmatch '[\\/]benchmarks[\\/]screenshots[\\/]' } |
    Sort-Object FullName | ForEach-Object FullName)
if ($Check) {
    & $formatterPath --dry-run --Werror $files
} else {
    & $formatterPath -i $files
}
if ($LASTEXITCODE -ne 0) { throw 'clang-format reported formatting differences.' }
