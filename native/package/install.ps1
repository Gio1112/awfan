[CmdletBinding()]
param(
    [string]$InstallDir = "",
    [switch]$NoPath,
    [switch]$NoBroker,
    [string]$TargetUser = "",
    [string]$TargetSid = "",
    [string]$PreviousInstallDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-SamePath {
    param(
        [Parameter(Mandatory)][string]$Left,
        [Parameter(Mandatory)][string]$Right
    )

    try {
        return [IO.Path]::GetFullPath($Left).TrimEnd("\") -ieq `
            [IO.Path]::GetFullPath($Right).TrimEnd("\")
    }
    catch {
        return $Left.TrimEnd("\") -ieq $Right.TrimEnd("\")
    }
}

function Copy-WithRetry {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination
    )

    for ($attempt = 1; $attempt -le 20; $attempt++) {
        try {
            Copy-Item -LiteralPath $Source -Destination $Destination -Force
            return
        }
        catch {
            if ($attempt -eq 20) {
                throw
            }
            Start-Sleep -Milliseconds 250
        }
    }
}

$userInstallDir = Join-Path $env:LOCALAPPDATA "Programs\awfan"
$secureInstallDir = Join-Path $env:ProgramFiles "awfan"

if (-not $InstallDir) {
    $InstallDir = if ($NoBroker) { $userInstallDir } else { $secureInstallDir }
}

if (-not $NoBroker -and -not (Test-SamePath $InstallDir $secureInstallDir)) {
    if (-not $PreviousInstallDir) {
        $PreviousInstallDir = $InstallDir
    }
    $InstallDir = $secureInstallDir
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

    if ($PreviousInstallDir) {
        $arguments += @("-PreviousInstallDir", "`"$PreviousInstallDir`"")
    }
    if ($NoPath) {
        $arguments += "-NoPath"
    }

    Write-Host "awfan needs one administrator approval to install its background broker."
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
        throw @"
The elevated installer is running as '$($identity.Name)', but awfan was started
by '$TargetUser'. The background broker must be installed by an administrator
account belonging to the same Windows user.
"@
    }
}

$sourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$requiredFiles = @(
    "awfan.exe",
    "awfan-core.exe",
    "awfan-broker.exe",
    "update.ps1",
    "uninstall.ps1",
    "awfan-completion.ps1",
    "README.txt",
    "CHANGELOG.txt",
    "THIRD-PARTY-NOTICES.txt",
    "LICENSE.txt"
)

foreach ($file in $requiredFiles) {
    $source = Join-Path $sourceDir $file
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required package file is missing: $source"
    }
}

$taskName = "awfan Broker $TargetSid"
$installedBroker = Join-Path $InstallDir "awfan-broker.exe"

if (-not $NoBroker) {
    foreach ($name in @($taskName, "awfan Broker")) {
        $existingTask = Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue
        if ($existingTask) {
            Stop-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue
            if ($name -eq "awfan Broker") {
                Unregister-ScheduledTask -TaskName $name -Confirm:$false -ErrorAction SilentlyContinue
            }
        }
    }

    Get-Process -Name "awfan-broker" -ErrorAction SilentlyContinue |
        Where-Object {
            try {
                $_.Path -and (
                    $_.Path -ieq $installedBroker -or
                    ($PreviousInstallDir -and $_.Path -ieq (Join-Path $PreviousInstallDir "awfan-broker.exe"))
                )
            }
            catch {
                $false
            }
        } |
        Stop-Process -Force -ErrorAction SilentlyContinue

    Start-Sleep -Milliseconds 400
}

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null

foreach ($file in $requiredFiles) {
    Copy-WithRetry `
        -Source (Join-Path $sourceDir $file) `
        -Destination (Join-Path $InstallDir $file)
}

if (-not $NoBroker) {
    $action = New-ScheduledTaskAction `
        -Execute $installedBroker `
        -WorkingDirectory $InstallDir
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $TargetUser
    $principal = New-ScheduledTaskPrincipal `
        -UserId $TargetUser `
        -LogonType Interactive `
        -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries `
        -StartWhenAvailable `
        -RestartCount 10 `
        -RestartInterval (New-TimeSpan -Minutes 1) `
        -ExecutionTimeLimit ([TimeSpan]::Zero)

    Register-ScheduledTask `
        -TaskName $taskName `
        -Action $action `
        -Trigger $trigger `
        -Principal $principal `
        -Settings $settings `
        -Description "Elevated AWCC broker for the awfan CLI" `
        -Force | Out-Null

    Start-ScheduledTask -TaskName $taskName
}

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$entries = @(
    ($userPath -split ";") |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ }
)

$entries = @($entries | Where-Object {
    -not (Test-SamePath $_ $InstallDir) -and
    (-not $PreviousInstallDir -or -not (Test-SamePath $_ $PreviousInstallDir))
})

if (-not $NoPath) {
    $entries += $InstallDir
}

[Environment]::SetEnvironmentVariable("Path", ($entries -join ";"), "User")

$installedExe = Join-Path $InstallDir "awfan.exe"
$version = & $installedExe version

Write-Host "Installed $version"
Write-Host "Location: $InstallDir"

if ($NoBroker) {
    Write-Host "Background broker: skipped"
} else {
    $brokerReady = $false
    for ($attempt = 1; $attempt -le 20; $attempt++) {
        & $installedExe broker-status *> $null
        if ($LASTEXITCODE -eq 0) {
            $brokerReady = $true
            break
        }
        Start-Sleep -Milliseconds 250
    }

    if (-not $brokerReady) {
        throw "The background broker was installed but did not become ready."
    }

    Write-Host "Background broker: installed and running"
}

if ($PreviousInstallDir -and
    -not (Test-SamePath $PreviousInstallDir $InstallDir) -and
    (Test-Path -LiteralPath $PreviousInstallDir)) {
    $command = "timeout /t 8 /nobreak >nul & rmdir /s /q `"$PreviousInstallDir`""
    Start-Process `
        -FilePath "$env:SystemRoot\System32\cmd.exe" `
        -ArgumentList @("/d", "/c", $command) `
        -WindowStyle Hidden | Out-Null
    Write-Host "Previous installation scheduled for removal: $PreviousInstallDir"
}

if ($NoPath) {
    Write-Host "PATH was not modified."
} else {
    Write-Host "Open a new terminal, then run:"
    Write-Host "  awfan broker-status"
    Write-Host "  awfan doctor"
    Write-Host "  awfan status"
}

Write-Host "Future updates:"
Write-Host "  awfan update"
