[CmdletBinding()]
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\awfan",
    [switch]$Uninstall,
    [switch]$NoPath,
    [switch]$NoBroker
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SourceRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildRoot = Join-Path $SourceRoot "build\native\Release"
$PackageRoot = Join-Path $SourceRoot "native\package"

if ($Uninstall) {
    $uninstaller = Join-Path $InstallDir "uninstall.ps1"
    if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
        throw "The native awfan uninstaller was not found at $uninstaller"
    }

    $arguments = @{ InstallDir = $InstallDir }
    if ($NoBroker) { $arguments.NoBroker = $true }
    & $uninstaller @arguments
    exit $LASTEXITCODE
}

$builtFiles = @(
    "awfan.exe",
    "awfan-core.exe",
    "awfan-broker.exe"
)

foreach ($file in $builtFiles) {
    $path = Join-Path $BuildRoot $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw @"
The native awfan binaries have not been built in this source checkout.

Build from source first:
  cmake -S native -B build/native -A x64
  cmake --build build/native --config Release
  .\install.ps1
"@
    }
}

$requiredPackageFiles = @(
    "install.ps1",
    "update.ps1",
    "uninstall.ps1",
    "awfan-completion.ps1",
    "README.txt",
    "CHANGELOG.txt",
    "THIRD-PARTY-NOTICES.txt",
    "LICENSE.txt"
)

foreach ($file in $requiredPackageFiles) {
    $path = Join-Path $PackageRoot $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required package file is missing: $path"
    }
}

$staging = Join-Path $env:TEMP "awfan-source-install-$PID"
Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $staging | Out-Null

try {
    foreach ($file in $builtFiles) {
        Copy-Item `
            -LiteralPath (Join-Path $BuildRoot $file) `
            -Destination (Join-Path $staging $file)
    }

    foreach ($file in $requiredPackageFiles) {
        Copy-Item `
            -LiteralPath (Join-Path $PackageRoot $file) `
            -Destination (Join-Path $staging $file)
    }

    $installer = Join-Path $staging "install.ps1"
    $arguments = @{ InstallDir = $InstallDir }
    if ($NoPath) { $arguments.NoPath = $true }
    if ($NoBroker) { $arguments.NoBroker = $true }

    & $installer @arguments
    exit $LASTEXITCODE
}
finally {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}
