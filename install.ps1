[CmdletBinding()]
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\awfan",
    [switch]$Uninstall,
    [switch]$NoPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SourceRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuiltExe = Join-Path $SourceRoot "build\native\Release\awfan.exe"
$PackageRoot = Join-Path $SourceRoot "native\package"

if ($Uninstall) {
    $uninstaller = Join-Path $InstallDir "uninstall.ps1"
    if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
        throw "The native awfan uninstaller was not found at $uninstaller"
    }

    & $uninstaller
    exit $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $BuiltExe -PathType Leaf)) {
    throw @"
The native awfan executable has not been built in this source checkout.

Recommended installation:
  Download the latest awfan Windows x64 ZIP from:
  https://github.com/Gio1112/awfan/releases/latest

Or build from source first:
  cmake -S native -B build/native -A x64
  cmake --build build/native --config Release
  .\install.ps1
"@
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
    Copy-Item -LiteralPath $BuiltExe -Destination (Join-Path $staging "awfan.exe")

    foreach ($file in $requiredPackageFiles) {
        Copy-Item `
            -LiteralPath (Join-Path $PackageRoot $file) `
            -Destination (Join-Path $staging $file)
    }

    $installer = Join-Path $staging "install.ps1"
    if ($NoPath) {
        & $installer -InstallDir $InstallDir -NoPath
    } else {
        & $installer -InstallDir $InstallDir
    }

    exit $LASTEXITCODE
}
finally {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}
