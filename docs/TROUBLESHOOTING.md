# Troubleshooting and safety

## Start with `doctor`

```powershell
awfan version
awfan doctor
awfan status
```

For an issue report, collect machine-readable output:

```powershell
awfan doctor --json
awfan status --json
```

Redact computer names, usernames, paths, service tags, serial numbers, and other personal information before posting logs.

## `AWCCWmiMethodFunction` was not found

awfan requires a compatible AWCC WMI interface in `ROOT\WMI`.

Check whether the class exists:

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

## `awfan` is not recognized after installation

Open a new terminal after running `install.ps1`.

Check the installation directory:

```powershell
Test-Path "$env:LOCALAPPDATA\Programs\awfan\awfan.exe"
```

Run it directly:

```powershell
& "$env:LOCALAPPDATA\Programs\awfan\awfan.exe" version
```

Inspect the user PATH:

```powershell
[Environment]::GetEnvironmentVariable("Path", "User")
```

## Windows SmartScreen warning

The current release is not code-signed. Windows may warn when opening the downloaded executable or installer.

Verify the release checksum before running it:

```powershell
$zip = ".\awfan-1.0.0-windows-x64.zip"
(Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
Get-Content ".\awfan-1.0.0-windows-x64.sha256"
```

The hashes must match.

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
& "$env:LOCALAPPDATA\Programs\awfan\uninstall.ps1"
```

Keep local state:

```powershell
& "$env:LOCALAPPDATA\Programs\awfan\uninstall.ps1" -KeepState
```

## Before opening an issue

Include:

- Exact model
- Windows version
- awfan version
- Whether the command was read-only or a write
- Redacted `doctor --json` output
- Redacted `status --json` output
- The complete error message

Never publish a security vulnerability or private data in a public issue.
