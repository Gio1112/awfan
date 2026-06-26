# awfan.ps1
# awfan v0.3 - non-elevated client for the elevated awfan broker.

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Command = "help",

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Arguments
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$TaskName = "awfan Broker"
$DataRoot = Join-Path $env:LOCALAPPDATA "awfan"
$QueueRoot = Join-Path $DataRoot "queue"
$HeartbeatFile = Join-Path $DataRoot "heartbeat.txt"
$InstallRoot = Join-Path $env:ProgramFiles "awfan"
$VersionFile = Join-Path $InstallRoot "VERSION"
$UpdateStatusFile = Join-Path $env:ProgramData "awfan\update-status.json"

function Show-Help {
    @"
awfan v0.3 - minimal Alienware fan-control CLI

The installer creates an elevated background broker. After that,
the commands below run from a normal, non-Administrator terminal.

Usage:
  awfan status
  awfan temps [seconds|once]
  awfan boost <cpu> <gpu>
  awfan balanced
  awfan cool
  awfan max
  awfan auto <profile-index>
  awfan restore <firmware-code>
  awfan watch [seconds]
  awfan service-status
  awfan service-start
  awfan service-stop
  awfan update
  awfan update-status
  awfan version
  awfan help

Examples:
  awfan status
  awfan temps
  awfan temps 1
  awfan temps once
  awfan boost 40 55
  awfan cool
  awfan restore a2

Notes:
  - 'temps' now refreshes every 2 seconds until Ctrl+C.
  - Use 'temps once' for a single temperature reading.
  - CPU is fan 0; GPU is fan 1.
"@ | Write-Host
}

function Initialize-DataFolders {
    New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $QueueRoot | Out-Null
}

function Test-BrokerHeartbeat {
    if (-not (Test-Path -LiteralPath $HeartbeatFile)) {
        return $false
    }

    try {
        $age = (Get-Date) - (Get-Item -LiteralPath $HeartbeatFile).LastWriteTime
        return $age.TotalSeconds -lt 5
    }
    catch {
        return $false
    }
}

function Start-Broker {
    if (Test-BrokerHeartbeat) {
        return
    }

    $started = $false

    try {
        Start-ScheduledTask -TaskName $TaskName -ErrorAction Stop
        $started = $true
    }
    catch {
        try {
            & "$env:SystemRoot\System32\schtasks.exe" /Run /TN $TaskName *> $null
            if ($LASTEXITCODE -eq 0) {
                $started = $true
            }
        }
        catch {
            $started = $false
        }
    }

    if (-not $started) {
        throw "The awfan broker could not be started. Run install.cmd once, accept the UAC prompt, and try again."
    }

    $deadline = (Get-Date).AddSeconds(10)

    while ((Get-Date) -lt $deadline) {
        if (Test-BrokerHeartbeat) {
            return
        }

        Start-Sleep -Milliseconds 150
    }

    throw "The awfan broker did not become ready within 10 seconds. Run 'awfan service-status'."
}

function Write-AtomicJson {
    param(
        [Parameter(Mandatory)]
        [object]$Value,

        [Parameter(Mandatory)]
        [string]$Destination
    )

    $temporary = "$Destination.tmp-$([guid]::NewGuid().ToString('N'))"
    $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination $Destination -Force
}

function Invoke-Broker {
    param(
        [Parameter(Mandatory)]
        [string]$Operation,

        [hashtable]$Payload = @{},

        [int]$TimeoutSeconds = 20
    )

    Initialize-DataFolders
    Start-Broker

    $id = [guid]::NewGuid().ToString("N")
    $requestPath = Join-Path $QueueRoot "$id.request.json"
    $responsePath = Join-Path $QueueRoot "$id.response.json"

    $request = @{
        id = $id
        operation = $Operation
        payload = $Payload
        createdAt = (Get-Date).ToUniversalTime().ToString("o")
    }

    Write-AtomicJson -Value $request -Destination $requestPath

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)

    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $responsePath) {
            try {
                $response = Get-Content -LiteralPath $responsePath -Raw |
                    ConvertFrom-Json

                if (-not $response.success) {
                    throw [string]$response.error
                }

                return $response
            }
            finally {
                Remove-Item -LiteralPath $requestPath -Force -ErrorAction SilentlyContinue
                Remove-Item -LiteralPath $responsePath -Force -ErrorAction SilentlyContinue
            }
        }

        Start-Sleep -Milliseconds 75
    }

    Remove-Item -LiteralPath $requestPath -Force -ErrorAction SilentlyContinue
    throw "Timed out waiting for the awfan broker."
}

function Write-ResponseLines {
    param(
        [Parameter(Mandatory)]
        [object]$Response
    )

    foreach ($line in @($Response.lines)) {
        Write-Host ([string]$line)
    }
}

function Get-IntegerArgument {
    param(
        [Parameter(Mandatory)]
        [int]$Index,

        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [int]$Minimum,

        [Parameter(Mandatory)]
        [int]$Maximum
    )

    if ($null -eq $Arguments -or $Arguments.Count -le $Index) {
        throw "Missing $Name. Expected an integer from $Minimum to $Maximum."
    }

    $value = 0

    if (-not [int]::TryParse($Arguments[$Index], [ref]$value)) {
        throw "$Name must be an integer."
    }

    if ($value -lt $Minimum -or $value -gt $Maximum) {
        throw "$Name must be from $Minimum to $Maximum."
    }

    return $value
}

function Get-RefreshInterval {
    param(
        [int]$Default = 2
    )

    if ($null -eq $Arguments -or $Arguments.Count -eq 0) {
        return $Default
    }

    $seconds = 0

    if (-not [int]::TryParse($Arguments[0], [ref]$seconds)) {
        throw "Refresh interval must be an integer from 1 to 60, or use 'once'."
    }

    if ($seconds -lt 1 -or $seconds -gt 60) {
        throw "Refresh interval must be from 1 to 60 seconds."
    }

    return $seconds
}

function Show-LiveView {
    param(
        [Parameter(Mandatory)]
        [ValidateSet("status", "temps")]
        [string]$Operation,

        [Parameter(Mandatory)]
        [int]$Seconds
    )

    while ($true) {
        $response = Invoke-Broker -Operation $Operation

        Clear-Host
        Write-Host "awfan $Operation - $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
        Write-Host ""

        Write-ResponseLines -Response $response

        Write-Host ""
        Write-Host "Refreshing every $Seconds second(s). Press Ctrl+C to stop."
        Start-Sleep -Seconds $Seconds
    }
}

$normalized = $Command.Trim().ToLowerInvariant()

switch ($normalized) {
    { $_ -in @("help", "-h", "--help", "/?") } {
        Show-Help
    }

    "status" {
        Write-ResponseLines -Response (Invoke-Broker -Operation "status")
    }

    "temps" {
        if (
            $null -ne $Arguments -and
            $Arguments.Count -gt 0 -and
            $Arguments[0].Trim().ToLowerInvariant() -eq "once"
        ) {
            Write-ResponseLines -Response (Invoke-Broker -Operation "temps")
        }
        else {
            $seconds = Get-RefreshInterval -Default 2
            Show-LiveView -Operation "temps" -Seconds $seconds
        }
    }

    "watch" {
        $seconds = Get-RefreshInterval -Default 2
        Show-LiveView -Operation "status" -Seconds $seconds
    }

    "boost" {
        $cpu = Get-IntegerArgument -Index 0 -Name "CPU boost" -Minimum 0 -Maximum 100
        $gpu = Get-IntegerArgument -Index 1 -Name "GPU boost" -Minimum 0 -Maximum 100

        Write-ResponseLines -Response (
            Invoke-Broker -Operation "boost" -Payload @{
                cpu = $cpu
                gpu = $gpu
            }
        )
    }

    "balanced" {
        Write-ResponseLines -Response (
            Invoke-Broker -Operation "boost" -Payload @{
                cpu = 25
                gpu = 25
            }
        )
    }

    "cool" {
        Write-ResponseLines -Response (
            Invoke-Broker -Operation "boost" -Payload @{
                cpu = 55
                gpu = 55
            }
        )
    }

    "max" {
        Write-ResponseLines -Response (
            Invoke-Broker -Operation "boost" -Payload @{
                cpu = 100
                gpu = 100
            }
        )
    }

    "auto" {
        $profile = Get-IntegerArgument -Index 0 -Name "profile index" -Minimum 1 -Maximum 5

        Write-ResponseLines -Response (
            Invoke-Broker -Operation "auto" -Payload @{
                profile = $profile
            }
        )
    }

    "restore" {
        if ($null -eq $Arguments -or $Arguments.Count -lt 1) {
            throw "Provide a firmware code, for example: awfan restore a2"
        }

        $code = $Arguments[0].Trim().ToLowerInvariant()

        if ($code -notmatch "^[0-9a-f]{1,4}$") {
            throw "Firmware code must be hexadecimal, for example a2."
        }

        Write-ResponseLines -Response (
            Invoke-Broker -Operation "restore" -Payload @{
                code = $code
            } -TimeoutSeconds 30
        )
    }

    "service-status" {
        if (Test-BrokerHeartbeat) {
            $age = (Get-Date) - (Get-Item -LiteralPath $HeartbeatFile).LastWriteTime
            Write-Host "awfan broker: running"
            Write-Host ("heartbeat age: {0:N1} seconds" -f $age.TotalSeconds)
        }
        else {
            Write-Host "awfan broker: not running or heartbeat is stale"
        }

        try {
            $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction Stop
            Write-Host "scheduled task: $($task.State)"
        }
        catch {
            Write-Host "scheduled task: unavailable or not installed"
        }
    }

    "service-start" {
        Start-Broker
        Write-Host "awfan broker started."
    }

    "service-stop" {
        if (-not (Test-BrokerHeartbeat)) {
            Write-Host "awfan broker is already stopped."
        }
        else {
            Write-ResponseLines -Response (
                Invoke-Broker -Operation "shutdown"
            )

            $deadline = (Get-Date).AddSeconds(5)
            while ((Get-Date) -lt $deadline -and (Test-BrokerHeartbeat)) {
                Start-Sleep -Milliseconds 150
            }
        }
    }


    "update" {
        Write-ResponseLines -Response (
            Invoke-Broker -Operation "start-update"
        )
        Write-Host "The updater is running in the background."
    }

    "update-status" {
        if (-not (Test-Path -LiteralPath $UpdateStatusFile)) {
            Write-Host "No updater status has been recorded yet."
        }
        else {
            Get-Content -LiteralPath $UpdateStatusFile -Raw | Write-Host
        }
    }

    "version" {
        if (Test-Path -LiteralPath $VersionFile) {
            Write-Host ("awfan " + (Get-Content -LiteralPath $VersionFile -Raw).Trim())
        }
        else {
            Write-Host "awfan version unknown"
        }
    }

    default {
        Write-Host "Unknown command: $Command"
        Write-Host ""
        Show-Help
        exit 1
    }
}
