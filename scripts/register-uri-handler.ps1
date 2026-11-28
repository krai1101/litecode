[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$ExecutablePath)
$ErrorActionPreference = 'Stop'
$resolvedExecutable = (Resolve-Path -LiteralPath $ExecutablePath).ProviderPath
if (-not (Test-Path -LiteralPath $resolvedExecutable -PathType Leaf) -or
    [IO.Path]::GetFileName($resolvedExecutable) -ine 'LiteCode.exe') {
    throw 'Specify the installed LiteCode.exe executable.'
}
# Explicit installer/user action only. Never run automatically at app startup.
# Registration is per user and does not require administrator privileges.
$protocolKey = 'HKCU:\Software\Classes\litecode'
New-Item -Path $protocolKey -Force | Out-Null
Set-Item -LiteralPath $protocolKey -Value 'URL:LiteCode Protocol'
New-ItemProperty -LiteralPath $protocolKey -Name 'URL Protocol' -Value '' -PropertyType String -Force | Out-Null
New-Item -Path "$protocolKey\DefaultIcon" -Force | Out-Null
Set-Item -LiteralPath "$protocolKey\DefaultIcon" -Value ('"{0}",0' -f $resolvedExecutable)
New-Item -Path "$protocolKey\shell\open\command" -Force | Out-Null
Set-Item -LiteralPath "$protocolKey\shell\open\command" -Value ('"{0}" --open-url "%1"' -f $resolvedExecutable)
Write-Output 'Registered litecode: URLs for this Windows user.'
