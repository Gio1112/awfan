# awfan

A lightweight command-line fan controller for the Alienware 16X Aurora AC16251.

awfan wraps `alienfan-cli.exe`, adds live temperature/status views, and uses a restricted elevated broker so normal commands do not need an Administrator terminal.

## Install

Requirements:

- Windows 11
- PowerShell 7 recommended
- Git
- AlienFX Tools extracted to `C:\Tools\AlienFan`

Clone and install:

```powershell
cd $HOME\dev
git clone https://github.com/Gio1112/awfan.git
cd awfan
.\install.cmd
```

Accept the one UAC prompt, then open a new normal PowerShell window.

## Commands

```powershell
awfan status
awfan temps
awfan temps 1
awfan temps once

awfan balanced
awfan cool
awfan max
awfan boost 40 55

awfan restore a2
awfan auto <profile-index>

awfan watch
awfan version
awfan update
awfan update-status
```

## Automatic updates

The installer creates an elevated `awfan Updater` scheduled task. It checks the `main` branch every 15 minutes, downloads only the files listed in `update-manifest.json`, verifies their SHA-256 hashes, installs them into `C:\Program Files\awfan`, and restarts the broker.

For a public repository, this works without credentials.

For a private repository, create a fine-grained GitHub token with **Contents: Read-only** access only to `Gio1112/awfan`, then run:

```powershell
.\configure-private-updates.ps1
```

The token is encrypted with Windows DPAPI for the current Windows account.

## Updating the source checkout

The installed runtime updates itself, but you can keep the source checkout current too:

```powershell
cd $HOME\dev\awfan
git pull --ff-only
```

## Safety

- Elevated tasks execute only from `C:\Program Files\awfan`, which normal processes cannot modify.
- The broker accepts only allowlisted fan, sensor, profile, and update operations.
- The updater downloads from the fixed `Gio1112/awfan` repository and verifies every runtime file against the commit-pinned manifest.
- `alienfan-cli.exe` remains a separate dependency and is not redistributed here.

## Uninstall

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1 -Uninstall
```
