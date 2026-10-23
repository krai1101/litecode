param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $root 'src'
$allowlistPath = Join-Path $PSScriptRoot 'architecture-allowlist.txt'
$violations = [System.Collections.Generic.List[string]]::new()
$observed = @{}
$allowances = @{}

function Get-RepositoryRelativePath {
    param([string]$Path)

    return $Path.Substring($root.Length + 1).Replace('\', '/')
}

function Get-SourceFiles {
    Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Where-Object {
        $_.Extension -in @('.cpp', '.h')
    }
}

function Add-RuleMatch {
    param(
        [string]$RuleId,
        [System.IO.FileInfo]$File,
        [string]$Text,
        [System.Text.RegularExpressions.Match]$Match,
        [string]$Reason
    )

    $relative = Get-RepositoryRelativePath -Path $File.FullName
    $key = "$RuleId|$relative"
    if (-not $observed.ContainsKey($key)) {
        $observed[$key] = 0
    }
    $observed[$key]++
    $prefix = $Text.Substring(0, $Match.Index)
    $line = 1 + ([regex]::Matches($prefix, "`n")).Count
    $violations.Add("$key|$line|$Reason")
}

function Find-Rule {
    param(
        [string]$RuleId,
        [string]$PathPattern,
        [string]$ContentPattern,
        [string]$Reason
    )

    foreach ($file in Get-SourceFiles) {
        $relative = Get-RepositoryRelativePath -Path $file.FullName
        if ($relative -notmatch $PathPattern) {
            continue
        }
        $text = [System.IO.File]::ReadAllText($file.FullName)
        foreach ($match in [regex]::Matches(
                $text,
                $ContentPattern,
                [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
            Add-RuleMatch -RuleId $RuleId -File $file -Text $text -Match $match -Reason $Reason
        }
    }
}

if (-not (Test-Path -LiteralPath $allowlistPath -PathType Leaf)) {
    throw "Architecture allowance file is missing: $allowlistPath"
}

foreach ($line in Get-Content -LiteralPath $allowlistPath) {
    $trimmed = $line.Trim()
    if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) {
        continue
    }
    $parts = $trimmed.Split('|', 4)
    if ($parts.Count -ne 4 -or -not ($parts[2] -as [int])) {
        throw "Malformed architecture allowance: $line"
    }
    $key = "$($parts[0])|$($parts[1])"
    if ($allowances.ContainsKey($key)) {
        throw "Duplicate architecture allowance: $key"
    }
    $allowances[$key] = [int]$parts[2]
}

$uiPathPattern = '^src/ui/'
Find-Rule -RuleId 'ui-concrete-service' -PathPattern $uiPathPattern `
    -ContentPattern '^\s*#\s*include\s*[<"]lsp/LspClient\.h[>"]' `
    -Reason 'UI code must depend on service interfaces, not concrete process services.'
Find-Rule -RuleId 'ui-service-construction' -PathPattern $uiPathPattern `
    -ContentPattern '\bnew\s+(?:lsp::LspClient|workspace::WorkspaceSearch)\b' `
    -Reason 'Long-lived services and workers must be constructed by the app composition root.'

Find-Rule -RuleId 'lower-ui-include' `
    -PathPattern '^src/(?:core|editor|lsp|terminal|workspace)/' `
    -ContentPattern '^\s*#\s*include\s*[<"](?:\.\./)*ui/' `
    -Reason 'Lower modules must not include the UI layer.'
Find-Rule -RuleId 'blocking-process-wait' -PathPattern '^src/' `
    -ContentPattern '\bwaitFor(?:Finished|ReadyRead)\s*\(' `
    -Reason 'Production code must not synchronously wait for a child process.'
Find-Rule -RuleId 'blocking-thread-join' -PathPattern '^src/' `
    -ContentPattern '\b(?:reader_|writer_|thread_|workerThread_)\s*\.\s*join\s*\(' `
    -Reason 'UI-reachable shutdown must be completion driven, not joined synchronously.'
Find-Rule -RuleId 'blocking-waitpid' -PathPattern '^src/' `
    -ContentPattern '\bwaitpid\s*\([^;\r\n]*,\s*0\s*\)' `
    -Reason 'UI-reachable teardown must not perform a blocking waitpid.'
Find-Rule -RuleId 'raw-win-owner' -PathPattern '^src/' `
    -ContentPattern '^\s*(?:HANDLE|HPCON|void\s*\*)\s+(?:pseudoConsole_|inputWrite_|outputRead_|processHandle_|process_)\s*(?:\{\}|=|;)' `
    -Reason 'Owning Win32 handles must use move-only RAII values.'
Find-Rule -RuleId 'unbounded-read' `
    -PathPattern '^src/(?:editor/DocumentSession\.cpp|lsp/|workspace/Workspace(?:Search|Replace)\.cpp)' `
    -ContentPattern '\breadAll(?:StandardOutput|StandardError)?\s*\(' `
    -Reason 'Document and process paths require an explicit size or retention bound.'
Find-Rule -RuleId 'unreviewed-permanent-delete' `
    -PathPattern '^src/(?!workspace/WorkspaceService\.cpp$)' `
    -ContentPattern '\b(?:removeRecursively|QFile::remove)\s*\(' `
    -Reason 'Permanent deletion must go through the identity-checked workspace service.'

$workflowRoot = Join-Path $root '.github\workflows'
if (Test-Path -LiteralPath $workflowRoot -PathType Container) {
    foreach ($workflow in Get-ChildItem -LiteralPath $workflowRoot -File | Where-Object {
            $_.Extension -in @('.yml', '.yaml')
        }) {
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $workflow.FullName) {
            ++$lineNumber
            if ($line -match '^\s*uses:\s*[^#\s]+@([^#\s]+)') {
                $revision = $Matches[1]
                if ($revision -notmatch '^[0-9a-fA-F]{40}$') {
                    $relative = Get-RepositoryRelativePath -Path $workflow.FullName
                    $violations.Add(
                        "unpinned-action|$relative|$lineNumber|GitHub Actions must use immutable 40-character commit IDs."
                    )
                    $observed["unpinned-action|$relative"] = 1
                }
            }
        }
    }
}

# Look for common UTF-8-as-Windows-1252 damage without embedding the damaged sequences here.
$mojibakeLeads = @([string][char]0x00C2, [string][char]0x00C3, [string][char]0x00E2,
                   [string][char]0xFFFD)
foreach ($file in Get-SourceFiles) {
    $text = [System.IO.File]::ReadAllText($file.FullName)
    foreach ($lead in $mojibakeLeads) {
        foreach ($match in [regex]::Matches($text, [regex]::Escape($lead))) {
            Add-RuleMatch -RuleId 'source-mojibake' -File $file -Text $text `
                -Match $match `
                -Reason 'Source contains a common mojibake or replacement-codepoint lead.'
        }
    }
}

$unexpected = [System.Collections.Generic.List[string]]::new()
foreach ($entry in $observed.GetEnumerator()) {
    $maximum = if ($allowances.ContainsKey($entry.Key)) { $allowances[$entry.Key] } else { 0 }
    if ($entry.Value -gt $maximum) {
        $matching = $violations | Where-Object { $_.StartsWith("$($entry.Key)|") }
        $unexpected.Add("$($entry.Key): found $($entry.Value), allowed $maximum")
        foreach ($detail in $matching) {
            $unexpected.Add("  $detail")
        }
    }
}

if ($unexpected.Count -gt 0) {
    $unexpected | ForEach-Object { Write-Error $_ }
    throw "Architecture check found new forbidden production code."
}

foreach ($entry in $allowances.GetEnumerator() | Sort-Object Key) {
    $count = if ($observed.ContainsKey($entry.Key)) { $observed[$entry.Key] } else { 0 }
    if ($count -lt $entry.Value) {
        Write-Warning "$($entry.Key): allowance can be reduced from $($entry.Value) to $count."
    }
}

Write-Host "LiteCode architecture boundaries passed ($($observed.Count) tracked debt locations)."
