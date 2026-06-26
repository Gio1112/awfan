# configure-private-updates.ps1
# Stores a fine-grained GitHub token encrypted with Windows DPAPI.
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Test-Admin {
    $p = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
if (-not (Test-Admin)) {
    $proc = Start-Process (Get-Process -Id $PID).Path -Verb RunAs -ArgumentList @("-NoProfile","-ExecutionPolicy","Bypass","-File","`"$PSCommandPath`"") -Wait -PassThru
    exit $proc.ExitCode
}

$secure = Read-Host "Paste a fine-grained GitHub token with read-only access to Gio1112/awfan" -AsSecureString
$encrypted = ConvertFrom-SecureString $secure
$dataRoot = Join-Path $env:ProgramData "awfan"
New-Item -ItemType Directory -Force -Path $dataRoot | Out-Null
$encrypted | Set-Content (Join-Path $dataRoot "github-token.dpapi") -Encoding ascii
Start-ScheduledTask -TaskName "awfan Updater" -ErrorAction SilentlyContinue
Write-Host "Private-repository updates configured."
