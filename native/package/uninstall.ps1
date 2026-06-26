[CmdletBinding()]
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\awfan",
    [switch]$KeepState
)

$ErrorActionPreference = "Stop"

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$entries = @(
    ($userPath -split ";") |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ }
)

$filtered = @($entries | Where-Object {
    $_.TrimEnd("\\") -ine $InstallDir.TrimEnd("\\")
})

[Environment]::SetEnvironmentVariable("Path", ($filtered -join ";"), "User")

if (Test-Path -LiteralPath $InstallDir) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
}

if (-not $KeepState) {
    $stateDir = Join-Path $env:LOCALAPPDATA "awfan"
    if (Test-Path -LiteralPath $stateDir) {
        Remove-Item -LiteralPath $stateDir -Recurse -Force
    }
}

Write-Host "awfan was uninstalled."
if ($KeepState) {
    Write-Host "Saved state was kept in $env:LOCALAPPDATA\awfan"
}
Write-Host "Open a new terminal to refresh PATH."
