param(
    [string]$Executable = "build/release/src/app/LiteCode.exe",
    [string]$OutputFile = "benchmarks/results/windows-local.json",
    [int]$IdleSeconds = 5,
    [int]$Samples = 5
)

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
$outputDirectory = Split-Path -Parent $OutputFile
if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}

$runs = @()
function Get-Median([double[]]$Values) {
    $ordered = @($Values | Sort-Object)
    if ($ordered.Count -eq 0) { return 0 }
    $middle = [Math]::Floor($ordered.Count / 2)
    if (($ordered.Count % 2) -eq 1) { return $ordered[$middle] }
    return ($ordered[$middle - 1] + $ordered[$middle]) / 2
}

for ($sample = 1; $sample -le $Samples; $sample++) {
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $resolvedExecutable -WindowStyle Hidden -PassThru
    $childIds = @()
    try {
        $responsive = $process.WaitForInputIdle(10000)
        $timer.Stop()
        Start-Sleep -Seconds $IdleSeconds
        $process.Refresh()
        $childRows = @()
        $childProcesses = @(Get-CimInstance Win32_Process | Where-Object {
            $_.ParentProcessId -eq $process.Id
        })
        $childIds = @($childProcesses.ProcessId)
        foreach ($child in $childProcesses) {
            $childProcess = Get-Process -Id $child.ProcessId -ErrorAction SilentlyContinue
            if ($null -ne $childProcess) {
                $childRows += [pscustomobject]@{
                    name = $child.Name
                    workingSetMiB = [Math]::Round($childProcess.WorkingSet64 / 1MB, 2)
                    privateMemoryMiB = [Math]::Round($childProcess.PrivateMemorySize64 / 1MB, 2)
                }
            }
        }
        $childWorkingSet = ($childRows | Measure-Object -Property workingSetMiB -Sum).Sum
        $childPrivateMemory = ($childRows | Measure-Object -Property privateMemoryMiB -Sum).Sum
        $runs += [pscustomobject]@{
            sample = $sample
            responsive = $responsive
            inputIdleMilliseconds = $timer.ElapsedMilliseconds
            workingSetMiB = [Math]::Round($process.WorkingSet64 / 1MB, 2)
            privateMemoryMiB = [Math]::Round($process.PrivateMemorySize64 / 1MB, 2)
            childWorkingSetMiB = [Math]::Round($childWorkingSet, 2)
            childPrivateMemoryMiB = [Math]::Round($childPrivateMemory, 2)
            aggregateWorkingSetMiB = [Math]::Round(($process.WorkingSet64 / 1MB) + $childWorkingSet, 2)
            aggregatePrivateMemoryMiB = [Math]::Round(($process.PrivateMemorySize64 / 1MB) + $childPrivateMemory, 2)
            childProcesses = $childRows
        }
    } finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
        foreach ($childId in $childIds) {
            Stop-Process -Id $childId -Force -ErrorAction SilentlyContinue
        }
    }
}
$orderedStartup = @($runs.inputIdleMilliseconds | Sort-Object)
$medianIndex = [Math]::Floor(($orderedStartup.Count - 1) / 2)
$p95Index = [Math]::Ceiling($orderedStartup.Count * 0.95) - 1
$measurement = [ordered]@{
        measuredAtUtc = [DateTime]::UtcNow.ToString("o")
        measurementKind = "Warm-cache process start to Windows input-idle"
        machine = $env:COMPUTERNAME
        os = [System.Environment]::OSVersion.VersionString
        processor = $env:PROCESSOR_IDENTIFIER
        executable = $resolvedExecutable
        samples = $Samples
        inputIdleMedianMilliseconds = $orderedStartup[$medianIndex]
        inputIdleP95Milliseconds = $orderedStartup[$p95Index]
        idleDelaySeconds = $IdleSeconds
        workingSetMedianMiB = Get-Median @($runs.workingSetMiB)
        privateMemoryMedianMiB = Get-Median @($runs.privateMemoryMiB)
        aggregateWorkingSetMedianMiB = Get-Median @($runs.aggregateWorkingSetMiB)
        aggregatePrivateMemoryMedianMiB = Get-Median @($runs.aggregatePrivateMemoryMiB)
        runs = $runs
    }
$measurement | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputFile -Encoding UTF8
$measurement | Format-List
