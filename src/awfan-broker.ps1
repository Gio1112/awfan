# awfan-broker.ps1
# Elevated background broker for awfan v0.2.
# This script is started by Task Scheduler with highest privileges.

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ToolRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ConfigFile = Join-Path $env:ProgramData "awfan\config.json"
$Backend = "C:\Tools\AlienFan\alienfan-cli.exe"
if (Test-Path -LiteralPath $ConfigFile) {
    try {
        $config = Get-Content -LiteralPath $ConfigFile -Raw | ConvertFrom-Json
        if (-not [string]::IsNullOrWhiteSpace([string]$config.backend)) {
            $Backend = [string]$config.backend
        }
    }
    catch {
        # Fall back to the standard AlienFX Tools path.
    }
}
$DataRoot = Join-Path $env:LOCALAPPDATA "awfan"
$QueueRoot = Join-Path $DataRoot "queue"
$HeartbeatFile = Join-Path $DataRoot "heartbeat.txt"
$StateFile = Join-Path $DataRoot "state.json"
$UpdaterTaskName = "awfan Updater"

$script:ShouldExit = $false
$script:LastHeartbeat = [datetime]::MinValue

function Initialize-Broker {
    if (-not (Test-Path -LiteralPath $Backend)) {
        throw "alienfan-cli.exe was not found: $Backend"
    }

    New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $QueueRoot | Out-Null

    Get-ChildItem -LiteralPath $QueueRoot -File -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -lt (Get-Date).AddMinutes(-10) } |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

function Update-Heartbeat {
    if (((Get-Date) - $script:LastHeartbeat).TotalMilliseconds -lt 750) {
        return
    }

    (Get-Date).ToUniversalTime().ToString("o") |
        Set-Content -LiteralPath $HeartbeatFile -Encoding ascii

    $script:LastHeartbeat = Get-Date
}

function Get-CleanOutput {
    param(
        [Parameter(Mandatory)]
        [string[]]$Lines
    )

    return @(
        $Lines |
            Where-Object {
                -not [string]::IsNullOrWhiteSpace($_) -and
                $_ -notmatch "^AlienFan-CLI v" -and
                $_ -notmatch "^Supported hardware "
            }
    )
}

function Invoke-Backend {
    param(
        [Parameter(Mandatory)]
        [string[]]$BackendArguments
    )

    Push-Location $ToolRoot

    try {
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"

        try {
            $rawOutput = @(
                & $Backend @BackendArguments 2>&1 |
                    ForEach-Object { $_.ToString() }
            )
            $exitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $previousPreference
        }
    }
    finally {
        Pop-Location
    }

    $output = Get-CleanOutput -Lines $rawOutput

    if (
        $exitCode -ne 0 -or
        ($output | Where-Object {
            $_ -match "Unknown command" -or
            $_ -match "Incorrect argument" -or
            $_ -match "^Error"
        })
    ) {
        $message = if ($output.Count -gt 0) {
            $output -join [Environment]::NewLine
        }
        else {
            "alienfan-cli.exe exited with code $exitCode."
        }

        throw $message
    }

    return [string[]]$output
}

function Save-State {
    param(
        [Parameter(Mandatory)]
        [hashtable]$State
    )

    $State["updatedAt"] = (Get-Date).ToUniversalTime().ToString("o")
    $temporary = "$StateFile.tmp"

    $State |
        ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $temporary -Encoding utf8

    Move-Item -LiteralPath $temporary -Destination $StateFile -Force
}

function Get-StateLines {
    if (-not (Test-Path -LiteralPath $StateFile)) {
        return @()
    }

    try {
        $state = Get-Content -LiteralPath $StateFile -Raw |
            ConvertFrom-Json

        if ($state.mode -eq "manual") {
            return @(
                "Requested boost: CPU $($state.cpu)%, GPU $($state.gpu)%",
                "Requested at: $($state.updatedAt)"
            )
        }

        if ($state.mode -eq "automatic") {
            $line = "Firmware profile index: $($state.profile)"

            if ($null -ne $state.code -and -not [string]::IsNullOrWhiteSpace([string]$state.code)) {
                $line += " ($($state.code))"
            }

            return @(
                $line,
                "Selected at: $($state.updatedAt)"
            )
        }
    }
    catch {
        return @("Saved awfan state could not be read.")
    }

    return @()
}

function Get-RequiredInteger {
    param(
        [Parameter(Mandatory)]
        [object]$Payload,

        [Parameter(Mandatory)]
        [string]$Property,

        [Parameter(Mandatory)]
        [int]$Minimum,

        [Parameter(Mandatory)]
        [int]$Maximum
    )

    $propertyValue = $Payload.PSObject.Properties[$Property]

    if ($null -eq $propertyValue) {
        throw "Missing request property: $Property"
    }

    $value = 0

    if (-not [int]::TryParse([string]$propertyValue.Value, [ref]$value)) {
        throw "$Property must be an integer."
    }

    if ($value -lt $Minimum -or $value -gt $Maximum) {
        throw "$Property must be from $Minimum to $Maximum."
    }

    return $value
}

function Invoke-Operation {
    param(
        [Parameter(Mandatory)]
        [string]$Operation,

        [Parameter(Mandatory)]
        [object]$Payload
    )

    switch ($Operation.Trim().ToLowerInvariant()) {
        "status" {
            $lines = @(
                Invoke-Backend -BackendArguments @(
                    "rpm",
                    "maxrpm",
                    "percent",
                    "getpower",
                    "getfans"
                )
            )

            $stateLines = @(Get-StateLines)

            if ($stateLines.Count -gt 0) {
                $lines += ""
                $lines += $stateLines
            }

            return $lines
        }

        "temps" {
            return @(
                Invoke-Backend -BackendArguments @("temp")
            )
        }

        "boost" {
            $cpu = Get-RequiredInteger -Payload $Payload -Property "cpu" -Minimum 0 -Maximum 100
            $gpu = Get-RequiredInteger -Payload $Payload -Property "gpu" -Minimum 0 -Maximum 100

            $lines = @(
                Invoke-Backend -BackendArguments @(
                    "unlock",
                    "setfans=$cpu,$gpu"
                )
            )

            Save-State @{
                mode = "manual"
                cpu = $cpu
                gpu = $gpu
            }

            $lines += ""
            $lines += "Requested boost: CPU $cpu%, GPU $gpu%."
            $lines += "Use 'awfan watch' to verify the RPM response."

            return $lines
        }

        "auto" {
            $profile = Get-RequiredInteger -Payload $Payload -Property "profile" -Minimum 1 -Maximum 5

            $lines = @(
                Invoke-Backend -BackendArguments @(
                    "setfans=0,0",
                    "setpower=$profile",
                    "getpower"
                )
            )

            Save-State @{
                mode = "automatic"
                profile = $profile
                code = ""
            }

            return $lines
        }

        "restore" {
            $codeProperty = $Payload.PSObject.Properties["code"]

            if ($null -eq $codeProperty) {
                throw "Missing request property: code"
            }

            $target = ([string]$codeProperty.Value).Trim().ToLowerInvariant()

            if ($target -notmatch "^[0-9a-f]{1,4}$") {
                throw "Firmware code must be hexadecimal, for example a2."
            }

            $result = [System.Collections.Generic.List[string]]::new()
            $result.Add("Searching firmware profiles 1-5 for code ($target)...")

            [void](Invoke-Backend -BackendArguments @("setfans=0,0"))

            foreach ($profile in 1..5) {
                $output = @(
                    Invoke-Backend -BackendArguments @(
                        "setpower=$profile",
                        "getpower"
                    )
                )

                $powerLine = $output |
                    Where-Object { $_ -match "^Power mode:" } |
                    Select-Object -Last 1

                if ([string]::IsNullOrWhiteSpace([string]$powerLine)) {
                    $powerLine = "Power mode not reported"
                }

                $result.Add("Index $profile -> $powerLine")

                if (
                    ([string]$powerLine).ToLowerInvariant() -match
                    "\($([regex]::Escape($target))\)"
                ) {
                    Save-State @{
                        mode = "automatic"
                        profile = $profile
                        code = $target
                    }

                    $result.Add("")
                    $result.Add("Restored firmware code ($target) using profile index $profile.")
                    return $result.ToArray()
                }

                Start-Sleep -Milliseconds 500
            }

            throw "No profile matched firmware code ($target). The last tested profile is currently active."
        }


        "start-update" {
            Start-ScheduledTask -TaskName $UpdaterTaskName
            return @("awfan updater started.")
        }

        "shutdown" {
            $script:ShouldExit = $true
            return @("awfan broker stopping.")
        }

        default {
            throw "Unsupported broker operation: $Operation"
        }
    }
}

function Write-Response {
    param(
        [Parameter(Mandatory)]
        [string]$ResponsePath,

        [Parameter(Mandatory)]
        [hashtable]$Response
    )

    $temporary = "$ResponsePath.tmp"

    $Response |
        ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $temporary -Encoding utf8

    Move-Item -LiteralPath $temporary -Destination $ResponsePath -Force
}

function Process-Request {
    param(
        [Parameter(Mandatory)]
        [System.IO.FileInfo]$RequestFile
    )

    $request = $null
    $responsePath = $null

    try {
        $request = Get-Content -LiteralPath $RequestFile.FullName -Raw |
            ConvertFrom-Json

        if ($null -eq $request.id -or [string]::IsNullOrWhiteSpace([string]$request.id)) {
            throw "Request ID is missing."
        }

        $responsePath = Join-Path $QueueRoot "$($request.id).response.json"
        $lines = @(
            Invoke-Operation -Operation ([string]$request.operation) -Payload $request.payload
        )

        Write-Response -ResponsePath $responsePath -Response @{
            id = [string]$request.id
            success = $true
            lines = $lines
            error = $null
        }
    }
    catch {
        if ($null -eq $responsePath) {
            $fallbackId = [System.IO.Path]::GetFileNameWithoutExtension(
                [System.IO.Path]::GetFileNameWithoutExtension($RequestFile.Name)
            )
            $responsePath = Join-Path $QueueRoot "$fallbackId.response.json"
        }

        Write-Response -ResponsePath $responsePath -Response @{
            id = if ($null -ne $request) { [string]$request.id } else { "" }
            success = $false
            lines = @()
            error = $_.Exception.Message
        }
    }
    finally {
        Remove-Item -LiteralPath $RequestFile.FullName -Force -ErrorAction SilentlyContinue
    }
}

Initialize-Broker

$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$mutex = [Threading.Mutex]::new($false, "Local\awfan-broker-$sid")
$hasMutex = $false

try {
    try {
        $hasMutex = $mutex.WaitOne(0)
    }
    catch [Threading.AbandonedMutexException] {
        $hasMutex = $true
    }

    if (-not $hasMutex) {
        exit 0
    }

    while (-not $script:ShouldExit) {
        Update-Heartbeat

        $requests = @(
            Get-ChildItem -LiteralPath $QueueRoot -Filter "*.request.json" -File |
                Sort-Object CreationTimeUtc
        )

        if ($requests.Count -eq 0) {
            Start-Sleep -Milliseconds 100
            continue
        }

        foreach ($request in $requests) {
            Process-Request -RequestFile $request

            if ($script:ShouldExit) {
                break
            }
        }
    }
}
finally {
    Remove-Item -LiteralPath $HeartbeatFile -Force -ErrorAction SilentlyContinue

    if ($hasMutex) {
        $mutex.ReleaseMutex()
    }

    $mutex.Dispose()
}
