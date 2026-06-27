# Troubleshooting and safety

## Start with these checks

```powershell
awfan version
awfan broker-status
awfan doctor
awfan status
```

For an issue report, collect machine-readable output:

```powershell
awfan doctor --json
awfan status --json
```

Redact computer names, usernames, paths, service tags, serial numbers, and other personal information before posting logs.

## Background broker is unavailable

Check the frontend first:

```powershell
awfan broker-status
```

A healthy installation reports:

```text
Background broker: running
Current process: standard user
```

The broker-enabled installation lives at:

```text
C:\Program Files\awfan
```

Check the files:

```powershell
Get-ChildItem "C:\Program Files\awfan"
```

Check the scheduled task from an elevated PowerShell:

```powershell
Get-ScheduledTask |
    Where-Object TaskName -Like "awfan Broker*" |
    Select-Object TaskName, State
```

Start it manually when present:

```powershell
Get-ScheduledTask |
    Where-Object TaskName -Like "awfan Broker*" |
    Start-ScheduledTask

Start-Sleep 1
awfan broker-status
```

If the task or protected binaries are missing, extract the latest release and run:

```powershell
.\install.ps1
```

Approve the administrator prompt. Do not copy `awfan-broker.exe` or `awfan-core.exe` into a user-writable folder and run them through an elevated scheduled task.

## Commands fail with `0x80041003`

`0x80041003` means the AWCC WMI provider denied access. With awfan 1.1.0 or newer, this normally means the command bypassed the broker or the broker is not running.

Run:

```powershell
awfan broker-status
Get-Command awfan | Select-Object Source
```

The command should resolve to:

```text
C:\Program Files\awfan\awfan.exe
```

An older `%LOCALAPPDATA%\Programs\awfan` entry may indicate that the 1.0.x installation is still earlier in your `PATH`. Re-run the current installer and open a new terminal.

A broker-free installation made with `-NoBroker` may still require an elevated terminal for AWCC commands.

## `AWCCWmiMethodFunction` was not found

awfan requires a compatible AWCC WMI interface in `ROOT\WMI`.

Check whether the class exists from an elevated PowerShell:

```powershell
Get-CimClass -Namespace ROOT\WMI -ClassName AWCCWmiMethodFunction
```

Possible causes include:

- The system is not supported
- Required Dell or Alienware components are missing
- The firmware exposes a different interface
- A Dell service or driver is disabled

Reboot once after installing or updating Alienware Command Center components.

## Read commands fail with `0x80041001`

`0x80041001` is a generic WMI provider failure. It can mean that the method, packed request, current controller state, or resource ID is not accepted by the system.

Run:

```powershell
awfan doctor
awfan exact-probe
awfan raw-probe
```

Attach the redacted output to a compatibility issue. Do not continue to write commands when the read-only probes fail.

## Fan percentage is unavailable

The tested AC16251 returns `-2` for the direct firmware fan-percentage query. awfan therefore reports nominal RPM utilization as:

```text
current RPM / reported nominal maximum RPM
```

This is telemetry, not the same as a firmware boost value.

## RPM exceeds the reported maximum

The firmware's maximum-RPM value is treated as nominal. Live readings may briefly exceed it because of telemetry timing, firmware behavior, or the way the nominal maximum is reported.

awfan displays values above 100% rather than silently clamping them.

## Manual boost behaves more aggressively than expected

Boost values are raw firmware control inputs from 0 to 100. They are **not** direct percentages and are **not** target RPM values.

On the tested AC16251, a value of `80` drove the fans close to maximum speed.

Return to firmware control:

```powershell
awfan profiles
awfan auto 1 --yes
```

Use the correct profile index for your system. Keep `awfan watch 1` open until RPM stabilizes.

## Profile `0` is rejected

This is intentional. On the tested system, selecting raw profile `0x00` did not reliably clear an existing manual boost. Use one of the discovered firmware profiles from 1 to 5 instead.

## Closing the lid stops monitoring

The broker is a Windows process. It pauses when Windows sleeps and continues after resume.

To keep the computer fully awake while plugged in with the lid closed, configure the plugged-in lid action to **Do nothing**. Keep the laptop on a hard surface with unobstructed vents.

## `awfan` is not recognized after installation

Open a new terminal after running `install.ps1`.

Check the protected installation:

```powershell
Test-Path "C:\Program Files\awfan\awfan.exe"
& "C:\Program Files\awfan\awfan.exe" version
```

Inspect the user `PATH`:

```powershell
[Environment]::GetEnvironmentVariable("Path", "User")
```

A `-NoBroker` installation uses:

```text
%LOCALAPPDATA%\Programs\awfan
```

## Windows blocks `install.ps1`

Unblock the downloaded ZIP before extraction:

```powershell
Unblock-File .\awfan-<version>-windows-x64.zip
```

For an already extracted package:

```powershell
Get-ChildItem -LiteralPath . -Recurse -File | Unblock-File
.\install.ps1
```

Do not permanently weaken the system execution policy.

## Windows SmartScreen warning

The current release is not code-signed. Windows may warn when opening the downloaded executable or installer.

Verify the release checksum before running it:

```powershell
$zip = ".\awfan-<version>-windows-x64.zip"
(Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
Get-Content ".\awfan-<version>-windows-x64.sha256"
```

The hashes must match.

## Update failed

Check the installed and latest versions:

```powershell
awfan version
awfan update --check
```

Broker-enabled updates may display one UAC prompt while replacing files in `C:\Program Files\awfan` and restarting the scheduled task.

After an update, open a new terminal and verify:

```powershell
awfan version
awfan broker-status
awfan doctor
```

## Clear awfan's remembered state

This clears only awfan's local command and RPM history. It does not itself change the current firmware profile or fan state.

```powershell
awfan clear-state
```

The state file is stored at:

```text
%LOCALAPPDATA%\awfan\state-v1.txt
```

## Uninstall

```powershell
& "C:\Program Files\awfan\uninstall.ps1"
```

Keep local state:

```powershell
& "C:\Program Files\awfan\uninstall.ps1" -KeepState
```

For a broker-free installation, use its `%LOCALAPPDATA%\Programs\awfan\uninstall.ps1` script with `-NoBroker`.

## Before opening an issue

Include:

- Exact model
- Windows version
- awfan version
- `awfan broker-status` output
- Whether the command was read-only or a write
- Redacted `doctor --json` output
- Redacted `status --json` output
- The complete error message

Never publish a security vulnerability or private data in a public issue.
