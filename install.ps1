# install.ps1
[CmdletBinding()]
param(
    [string]$Backend = "C:\Tools\AlienFan\alienfan-cli.exe",
    [switch]$Uninstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$InstallRoot = Join-Path $env:ProgramFiles "awfan"
$DataRoot = Join-Path $env:ProgramData "awfan"
$BrokerTaskName = "awfan Broker"
$UpdaterTaskName = "awfan Updater"
$SourceRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

function Test-Admin {
    $p = [Security.Principal.WindowsPrincipal]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent()
    )
    $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Admin)) {
    $args = @("-NoProfile","-ExecutionPolicy","Bypass","-File","`"$PSCommandPath`"","-Backend","`"$Backend`"")
    if ($Uninstall) { $args += "-Uninstall" }
    $proc = Start-Process (Get-Process -Id $PID).Path -Verb RunAs -ArgumentList $args -Wait -PassThru
    exit $proc.ExitCode
}

if ($Uninstall) {
    Stop-ScheduledTask -TaskName $BrokerTaskName -ErrorAction SilentlyContinue
    Stop-ScheduledTask -TaskName $UpdaterTaskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $BrokerTaskName -Confirm:$false -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $UpdaterTaskName -Confirm:$false -ErrorAction SilentlyContinue
    Remove-Item $InstallRoot -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "awfan removed."
    exit 0
}

if (-not (Test-Path -LiteralPath $Backend)) {
    throw "alienfan-cli.exe was not found at $Backend"
}

New-Item -ItemType Directory -Force -Path $InstallRoot, $DataRoot | Out-Null

$mapping = @{
    "src\awfan.ps1" = "awfan.ps1"
    "src\awfan-broker.ps1" = "awfan-broker.ps1"
    "src\awfan-updater.ps1" = "awfan-updater.ps1"
    "bin\awfan.cmd" = "awfan.cmd"
    "VERSION" = "VERSION"
}

foreach ($item in $mapping.GetEnumerator()) {
    Copy-Item (Join-Path $SourceRoot $item.Key) (Join-Path $InstallRoot $item.Value) -Force
}

@{ backend = $Backend } | ConvertTo-Json |
    Set-Content (Join-Path $DataRoot "config.json") -Encoding utf8

# Broker currently expects the standard backend location used by this project.
# Keep a compatibility copy of the backend path in the process environment.
[Environment]::SetEnvironmentVariable("AWFAN_BACKEND", $Backend, "User")

$shell = (Get-Command pwsh.exe -ErrorAction SilentlyContinue).Source
if (-not $shell) { $shell = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" }
$user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -MultipleInstances IgnoreNew

$brokerAction = New-ScheduledTaskAction -Execute $shell -Argument "-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$InstallRoot\awfan-broker.ps1`""
$brokerTrigger = New-ScheduledTaskTrigger -AtLogOn -User $user
Register-ScheduledTask -TaskName $BrokerTaskName -Action $brokerAction -Trigger $brokerTrigger -Principal $principal -Settings $settings -Force | Out-Null

$updateAction = New-ScheduledTaskAction -Execute $shell -Argument "-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$InstallRoot\awfan-updater.ps1`""
$updateTrigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) -RepetitionInterval (New-TimeSpan -Minutes 15) -RepetitionDuration (New-TimeSpan -Days 3650)
Register-ScheduledTask -TaskName $UpdaterTaskName -Action $updateAction -Trigger $updateTrigger -Principal $principal -Settings $settings -Force | Out-Null

$userPath = [Environment]::GetEnvironmentVariable("Path","User")
if (($userPath -split ";") -notcontains $InstallRoot) {
    [Environment]::SetEnvironmentVariable("Path", (($userPath.TrimEnd(";") + ";" + $InstallRoot).TrimStart(";")), "User")
}

$commit = ""
try { $commit = (& git -C $SourceRoot rev-parse HEAD 2>$null).Trim() } catch {}
if ($commit) { $commit | Set-Content (Join-Path $DataRoot "installed-commit.txt") -Encoding ascii }

Start-ScheduledTask -TaskName $BrokerTaskName
Start-ScheduledTask -TaskName $UpdaterTaskName

Write-Host ""
Write-Host "awfan installed to $InstallRoot"
Write-Host "Open a new normal PowerShell window and run: awfan status"
