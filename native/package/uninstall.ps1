[CmdletBinding()]
param(
    [string]$InstallDir = "",
    [switch]$KeepState,
    [switch]$NoBroker,
    [string]$TargetUser = "",
    [string]$TargetSid = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not $InstallDir) {
    $InstallDir = Split-Path -Parent $PSCommandPath
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $TargetUser) {
    $TargetUser = $identity.Name
}
if (-not $TargetSid) {
    $TargetSid = $identity.User.Value
}

if (-not $NoBroker -and -not (Test-Administrator)) {
    $powershell = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
    $arguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"",
        "-InstallDir", "`"$InstallDir`"",
        "-TargetUser", "`"$TargetUser`"",
        "-TargetSid", "`"$TargetSid`""
    )

    if ($KeepState) {
        $arguments += "-KeepState"
    }

    $process = Start-Process `
        -FilePath $powershell `
        -Verb RunAs `
        -ArgumentList ($arguments -join " ") `
        -Wait `
        -PassThru

    exit $process.ExitCode
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $NoBroker) {
    if ($identity.Name -ine $TargetUser -or $identity.User.Value -ine $TargetSid) {
        throw "The elevated uninstaller must run as the same Windows user that installed awfan."
    }

    foreach ($taskName in @("awfan Broker $TargetSid", "awfan Broker")) {
        if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
            Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
            Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
        }
    }

    $installedBroker = Join-Path $InstallDir "awfan-broker.exe"
    Get-Process -Name "awfan-broker" -ErrorAction SilentlyContinue |
        Where-Object {
            try {
                $_.Path -and $_.Path -ieq $installedBroker
            }
            catch {
                $false
            }
        } |
        Stop-Process -Force -ErrorAction SilentlyContinue

    Start-Sleep -Milliseconds 400
}

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$entries = @(
    ($userPath -split ";") |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ }
)

$filtered = @($entries | Where-Object {
    $_.TrimEnd("\") -ine $InstallDir.TrimEnd("\")
})

[Environment]::SetEnvironmentVariable("Path", ($filtered -join ";"), "User")

if (Test-Path -LiteralPath $InstallDir) {
    if ($NoBroker) {
        Remove-Item -LiteralPath $InstallDir -Recurse -Force
    } else {
        $command = "timeout /t 3 /nobreak >nul & rmdir /s /q `"$InstallDir`""
        Start-Process `
            -FilePath "$env:SystemRoot\System32\cmd.exe" `
            -ArgumentList @("/d", "/c", $command) `
            -WindowStyle Hidden | Out-Null
    }
}

if (-not $KeepState) {
    $stateDir = Join-Path $env:LOCALAPPDATA "awfan"
    if (Test-Path -LiteralPath $stateDir) {
        Remove-Item -LiteralPath $stateDir -Recurse -Force
    }
}

Write-Host "awfan was uninstalled."
if ($KeepState) {
    Write-Host "Saved state was kept in $env:LOCALAPPDATA\awfan"
}
Write-Host "Open a new terminal to refresh PATH."
