[CmdletBinding()]
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\awfan",
    [int]$ParentPid = 0,
    [switch]$CheckOnly,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repository = "Gio1112/awfan"
$headers = @{
    Accept = "application/vnd.github+json"
    "User-Agent" = "awfan-updater"
    "X-GitHub-Api-Version" = "2022-11-28"
}

function Get-InstalledVersion {
    $exe = Join-Path $InstallDir "awfan.exe"
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "awfan.exe was not found at $exe"
    }

    $text = (& $exe version | Out-String).Trim()
    if ($text -notmatch '^awfan\s+([0-9]+\.[0-9]+\.[0-9]+)$') {
        throw "Could not parse the installed version from '$text'."
    }

    return [Version]$Matches[1]
}

function Get-ReleaseAsset {
    param(
        [Parameter(Mandatory)]$Release,
        [Parameter(Mandatory)][string]$Name
    )

    $asset = @($Release.assets | Where-Object { $_.name -eq $Name }) |
        Select-Object -First 1

    if (-not $asset) {
        throw "The latest release does not contain $Name"
    }

    return $asset
}

$currentVersion = Get-InstalledVersion
Write-Host "Installed version: $currentVersion"
Write-Host "Checking GitHub Releases..."

$release = Invoke-RestMethod `
    -Uri "https://api.github.com/repos/$repository/releases/latest" `
    -Headers $headers `
    -Method Get

$tag = [string]$release.tag_name
if ($tag -notmatch '^v([0-9]+\.[0-9]+\.[0-9]+)$') {
    throw "The latest release tag '$tag' is not a supported stable version."
}

$latestVersion = [Version]$Matches[1]
Write-Host "Latest version:    $latestVersion"

if (-not $Force -and $latestVersion -le $currentVersion) {
    Write-Host "awfan is already up to date."
    exit 0
}

if ($CheckOnly) {
    Write-Host "Update available: $currentVersion -> $latestVersion"
    exit 0
}

$zipName = "awfan-$latestVersion-windows-x64.zip"
$checksumName = "awfan-$latestVersion-windows-x64.sha256"
$zipAsset = Get-ReleaseAsset -Release $release -Name $zipName
$checksumAsset = Get-ReleaseAsset -Release $release -Name $checksumName

$tempRoot = Join-Path $env:TEMP "awfan-update-$PID"
$zipPath = Join-Path $tempRoot $zipName
$checksumPath = Join-Path $tempRoot $checksumName
$extractPath = Join-Path $tempRoot "extracted"

Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $tempRoot | Out-Null

try {
    Write-Host "Downloading $zipName..."
    Invoke-WebRequest `
        -Uri $zipAsset.browser_download_url `
        -Headers $headers `
        -OutFile $zipPath `
        -UseBasicParsing

    Invoke-WebRequest `
        -Uri $checksumAsset.browser_download_url `
        -Headers $headers `
        -OutFile $checksumPath `
        -UseBasicParsing

    $expectedHash = ((Get-Content -LiteralPath $checksumPath -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
    $actualHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()

    if ($expectedHash -notmatch '^[0-9a-f]{64}$') {
        throw "The release checksum file is invalid."
    }

    if ($actualHash -ne $expectedHash) {
        throw "SHA-256 verification failed. The package was not installed."
    }

    Write-Host "Checksum verified."
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractPath -Force
    Get-ChildItem -LiteralPath $extractPath -Recurse -File | Unblock-File

    $installer = Get-ChildItem `
        -LiteralPath $extractPath `
        -Filter "install.ps1" `
        -File `
        -Recurse |
        Select-Object -First 1

    if (-not $installer) {
        throw "The downloaded package does not contain install.ps1."
    }

    if ($ParentPid -gt 0) {
        Write-Host "Waiting for awfan to exit..."
        Wait-Process -Id $ParentPid -ErrorAction SilentlyContinue
    }

    Write-Host "Installing awfan $latestVersion..."
    & $installer.FullName -InstallDir $InstallDir

    Write-Host "Updated awfan to $latestVersion."
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
