# awfan-updater.ps1
# Elevated, allowlisted updater for awfan.

[CmdletBinding()]
param([switch]$Force)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Owner = "Gio1112"
$Repository = "awfan"
$Branch = "main"
$InstallRoot = Join-Path $env:ProgramFiles "awfan"
$DataRoot = Join-Path $env:ProgramData "awfan"
$StatusFile = Join-Path $DataRoot "update-status.json"
$CommitFile = Join-Path $DataRoot "installed-commit.txt"
$TokenFile = Join-Path $DataRoot "github-token.dpapi"
$BrokerTaskName = "awfan Broker"

function Save-Status([string]$State, [string]$Message, [string]$Commit = "") {
    New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null
    @{
        state = $State
        message = $Message
        commit = $Commit
        checkedAt = (Get-Date).ToUniversalTime().ToString("o")
    } | ConvertTo-Json | Set-Content -LiteralPath $StatusFile -Encoding utf8
}

function Get-Headers {
    $headers = @{
        "Accept" = "application/vnd.github+json"
        "User-Agent" = "awfan-updater"
        "X-GitHub-Api-Version" = "2022-11-28"
    }

    if (Test-Path -LiteralPath $TokenFile) {
        $encrypted = Get-Content -LiteralPath $TokenFile -Raw
        $secure = ConvertTo-SecureString $encrypted
        $token = [System.Net.NetworkCredential]::new("", $secure).Password
        if (-not [string]::IsNullOrWhiteSpace($token)) {
            $headers["Authorization"] = "Bearer $token"
        }
    }

    return $headers
}

function Download-RepoFile([string]$Path, [string]$Ref, [string]$Destination, [hashtable]$Headers) {
    $escaped = ($Path -split "/" | ForEach-Object { [uri]::EscapeDataString($_) }) -join "/"
    $url = "https://api.github.com/repos/$Owner/$Repository/contents/$escaped?ref=$Ref"
    $rawHeaders = @{} + $Headers
    $rawHeaders["Accept"] = "application/vnd.github.raw+json"
    Invoke-WebRequest -Uri $url -Headers $rawHeaders -OutFile $Destination
}

try {
    Save-Status "checking" "Checking for updates."

    $headers = Get-Headers
    $commitInfo = Invoke-RestMethod `
        -Uri "https://api.github.com/repos/$Owner/$Repository/commits/$Branch" `
        -Headers $headers

    $remoteCommit = [string]$commitInfo.sha
    $installedCommit = if (Test-Path -LiteralPath $CommitFile) {
        (Get-Content -LiteralPath $CommitFile -Raw).Trim()
    } else { "" }

    if (-not $Force -and $remoteCommit -eq $installedCommit) {
        Save-Status "current" "awfan is already up to date." $remoteCommit
        exit 0
    }

    $staging = Join-Path $env:TEMP ("awfan-update-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $staging | Out-Null

    try {
        $manifestPath = Join-Path $staging "update-manifest.json"
        Download-RepoFile "update-manifest.json" $remoteCommit $manifestPath $headers
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

        foreach ($file in $manifest.files) {
            $destinationName = [string]$file.destination
            if ($destinationName -notmatch "^[A-Za-z0-9._-]+$") {
                throw "Unsafe destination in update manifest: $destinationName"
            }

            $downloadPath = Join-Path $staging $destinationName
            Download-RepoFile ([string]$file.source) $remoteCommit $downloadPath $headers

            $actual = (Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash.ToLowerInvariant()
            $expected = ([string]$file.sha256).ToLowerInvariant()
            if ($actual -ne $expected) {
                throw "Hash mismatch for $($file.source)"
            }
        }

        Stop-ScheduledTask -TaskName $BrokerTaskName -ErrorAction SilentlyContinue

        foreach ($file in $manifest.files) {
            Copy-Item `
                -LiteralPath (Join-Path $staging ([string]$file.destination)) `
                -Destination (Join-Path $InstallRoot ([string]$file.destination)) `
                -Force
        }

        $remoteCommit | Set-Content -LiteralPath $CommitFile -Encoding ascii
        Start-ScheduledTask -TaskName $BrokerTaskName
        Save-Status "updated" "Updated awfan to $($manifest.version)." $remoteCommit
    }
    finally {
        Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    }
}
catch {
    Save-Status "error" $_.Exception.Message
    exit 1
}
