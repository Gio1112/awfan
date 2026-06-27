[CmdletBinding()]
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\awfan",
    [switch]$NoPath
)

$ErrorActionPreference = "Stop"

$sourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceExe = Join-Path $sourceDir "awfan.exe"

if (-not (Test-Path -LiteralPath $sourceExe -PathType Leaf)) {
    throw "awfan.exe was not found next to install.ps1. Extract the full package first."
}

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null

$files = @(
    "awfan.exe",
    "update.ps1",
    "uninstall.ps1",
    "awfan-completion.ps1",
    "README.txt",
    "CHANGELOG.txt",
    "THIRD-PARTY-NOTICES.txt",
    "LICENSE.txt"
)

foreach ($file in $files) {
    $source = Join-Path $sourceDir $file
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $InstallDir $file) -Force
    }
}

if (-not $NoPath) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $entries = @(
        ($userPath -split ";") |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ }
    )

    $alreadyPresent = $entries | Where-Object {
        $_.TrimEnd("\\") -ieq $InstallDir.TrimEnd("\\")
    }

    if (-not $alreadyPresent) {
        $newPath = (@($entries) + $InstallDir) -join ";"
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    }

    if (($env:Path -split ";") -notcontains $InstallDir) {
        $env:Path = "$InstallDir;$env:Path"
    }
}

$installedExe = Join-Path $InstallDir "awfan.exe"
$version = & $installedExe version

Write-Host "Installed $version"
Write-Host "Location: $InstallDir"

if ($NoPath) {
    Write-Host "PATH was not modified. Run the executable using:"
    Write-Host "  & '$installedExe' status"
} else {
    Write-Host "Open a new terminal, then run:"
    Write-Host "  awfan doctor"
    Write-Host "  awfan status"
    Write-Host "  awfan update --check"
}

Write-Host "Optional PowerShell completion:"
Write-Host "  . '$InstallDir\awfan-completion.ps1'"
