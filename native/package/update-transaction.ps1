[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet("Backup", "Health", "Restore")]
    [string]$Action,

    [Parameter(Mandatory)]
    [string]$InstallDir,

    [Parameter(Mandatory)]
    [string]$BackupDir,

    [string]$ExpectedVersion = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

switch ($Action) {
    "Backup" {
        Remove-Item -LiteralPath $BackupDir -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null

        Get-ChildItem -LiteralPath $InstallDir -Force | ForEach-Object {
            Copy-Item `
                -LiteralPath $_.FullName `
                -Destination $BackupDir `
                -Recurse `
                -Force
        }

        if (-not (Test-Path -LiteralPath (Join-Path $BackupDir "install.ps1"))) {
            throw "The rollback backup does not contain install.ps1."
        }
    }

    "Health" {
        if (-not $ExpectedVersion) {
            throw "ExpectedVersion is required for a health check."
        }

        $exe = Join-Path $InstallDir "awfan.exe"
        if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
            throw "The installed awfan.exe is missing."
        }

        $actualVersion = (& $exe version | Out-String).Trim()
        if ($actualVersion -ne "awfan $ExpectedVersion") {
            throw "Expected awfan $ExpectedVersion but found '$actualVersion'."
        }

        foreach ($file in @("awfan-core.exe", "awfan-broker.exe")) {
            if (-not (Test-Path -LiteralPath (Join-Path $InstallDir $file) -PathType Leaf)) {
                throw "Installed component is missing: $file"
            }
        }

        & $exe broker status --json | ConvertFrom-Json | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "The awfan broker health check failed."
        }

        & $exe doctor --json | ConvertFrom-Json | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "The AWCC health check failed."
        }
    }

    "Restore" {
        $installer = Join-Path $BackupDir "install.ps1"
        if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
            throw "The rollback installer is missing."
        }

        & $installer -InstallDir $InstallDir
        if ($LASTEXITCODE -ne 0) {
            throw "The rollback installer exited with code $LASTEXITCODE."
        }
    }
}
