[CmdletBinding()]
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\awfan",
    [switch]$NoPath,
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
    $existingTask = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($existingTask) {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    }

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

if (-not $NoPath) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $entries = @(
        ($userPath -split ";") |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ }
    )

    $alreadyPresent = $entries | Where-Object {
        $_.TrimEnd("\") -ieq $InstallDir.TrimEnd("\")
    }

    if (-not $alreadyPresent) {
        $newPath = (@($entries) + $InstallDir) -join ";"
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    }
}

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
