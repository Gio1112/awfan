Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

throw @"
configure-private-updates.ps1 belonged to the retired PowerShell prototype.

The native awfan 1.x release does not store a GitHub token or use the old
scheduled updater. Install updates from the public GitHub Releases page:

  https://github.com/Gio1112/awfan/releases/latest

See README.md for the supported installation process.
"@
