<div align="center">

# awfan

**A lightweight native fan and thermal CLI for Alienware systems on Windows.**

[![Release](https://img.shields.io/github/v/release/Gio1112/awfan?display_name=tag&sort=semver)](https://github.com/Gio1112/awfan/releases/latest)
[![Windows build](https://github.com/Gio1112/awfan/actions/workflows/native-probe.yml/badge.svg)](https://github.com/Gio1112/awfan/actions/workflows/native-probe.yml)
[![Windows x64](https://img.shields.io/badge/platform-Windows%20x64-0078D4)](https://github.com/Gio1112/awfan/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-black.svg)](LICENSE)

</div>

awfan communicates directly with the Alienware Command Center WMI interface. The native application is written in C++20 and does **not** require AlienFan CLI or Alienware Command Center to be open at runtime.

> [!IMPORTANT]
> awfan is an independent community project. It is not affiliated with or endorsed by Dell Technologies or Alienware. Fan-control writes remain experimental and should be used carefully.

## Features

- Live CPU and GPU temperatures
- Current and nominal maximum fan RPM
- Fan-to-sensor discovery
- Rising, falling, and stable RPM trend detection
- Current and available AWCC thermal profiles
- Human-readable and versioned JSON output
- Native manual fan-boost and firmware-profile controls
- Per-user elevated background broker for normal, non-admin terminals
- Verified self-updates from GitHub Releases
- Portable Windows package with installer, uninstaller, checksum, and PowerShell completion

## Compatibility

| System | Read support | Write support | Status |
| --- | --- | --- | --- |
| Alienware 16X Aurora AC16251 | Tested | Tested | Supported reference system |
| Other Alienware systems exposing `ROOT\WMI:AWCCWmiMethodFunction` | Possible | Untested | Community testing needed |

The tested AC16251 exposes two fans, CPU and GPU thermal sensors, and five firmware thermal profiles. Other systems may use different resource IDs or behavior.

See [Compatibility](docs/COMPATIBILITY.md) before testing another model.

## Install

### Recommended installation

1. Download `awfan-<version>-windows-x64.zip` and its matching `.sha256` file from the [latest release](https://github.com/Gio1112/awfan/releases/latest).
2. Unblock the ZIP before extracting it:

```powershell
Unblock-File .\awfan-<version>-windows-x64.zip
```

3. Extract the ZIP and open PowerShell in the extracted folder.
4. Run:

```powershell
.\install.ps1
```

5. Approve the single administrator prompt used to place the privileged broker components in `C:\Program Files\awfan` and register their scheduled task.
6. Open a new terminal and verify:

```powershell
awfan version
awfan broker-status
awfan doctor
awfan status
```

The public `awfan.exe` command remains available from your user `PATH`. Hardware commands are forwarded to the elevated broker, so normal terminals no longer need repeated UAC prompts.

### Background broker

The installer registers `awfan-broker.exe` as a per-user elevated scheduled task. It starts at sign-in, restarts after failures, and becomes available after resume.

The broker:

- Accepts only an explicit allowlist of awfan hardware commands
- Uses a local named pipe restricted to the current user, Administrators, and SYSTEM
- Rejects remote pipe clients
- Launches the protected `awfan-core.exe` directly without a command shell
- Preserves the `--yes` requirement for every hardware write

Check it at any time:

```powershell
awfan broker-status
```

> [!NOTE]
> The broker cannot execute while Windows itself is asleep. It resumes with Windows. Closing the lid only keeps awfan active when the plugged-in lid action is configured not to sleep.

### Broker-free installation

For portable use, CI, or advanced troubleshooting, install without the scheduled broker:

```powershell
.\install.ps1 -NoBroker
```

This uses `%LOCALAPPDATA%\Programs\awfan`. Hardware commands may require an elevated terminal in this mode.

### Updating

Starting with awfan 1.0.1, stable releases can be installed without manually downloading every ZIP:

```powershell
awfan update --check
awfan update
```

The updater downloads the latest stable package and matching SHA-256 checksum from GitHub Releases, verifies the package, waits for the current awfan process to exit, and runs the packaged installer. Broker updates may request one administrator approval while replacing the protected files and scheduled task.

### Portable use

You can run the extracted binaries without installation from an elevated terminal:

```powershell
.\awfan.exe doctor
.\awfan.exe status
```

> [!NOTE]
> The current executable is not code-signed. Windows SmartScreen may display a warning when the package is first opened.

## Quick start

```powershell
# Full status
awfan status

# Machine-readable status
awfan status --json

# Live temperatures and fan telemetry
awfan watch 2

# List discovered profiles
awfan profiles
awfan presets
```

### Control commands

Every hardware-changing command requires `--yes`:

```powershell
# Raw firmware fan-boost values, not percentages or target RPM
awfan boost 20 20 --yes

# Maximum boost
awfan max --yes

# Select manual profile 0 without applying a boost value
awfan profile 0 --yes

# Return to a discovered firmware profile
awfan auto 1 --yes
```

Known profile mappings on the AC16251:

| Index | Raw ID | Profile |
| ---: | ---: | --- |
| 1 | `0xA0` | Balanced |
| 2 | `0xA1` | Balanced Performance |
| 3 | `0xA2` | Cool |
| 4 | `0xA3` | Quiet |
| 5 | `0xA4` | Performance |

Profile `0` selects manual mode without applying or remembering a boost value. It remains diagnostic-only because it did not reliably clear an existing manual boost during testing.

## Command reference

| Command | Description |
| --- | --- |
| `awfan status [--json]` | Show fans, temperatures, power profile, and controller state |
| `awfan fans [--json]` | Show fan telemetry and RPM trends |
| `awfan temps [once\|seconds] [--json]` | Show temperatures once or refresh continuously |
| `awfan watch [seconds]` | Live full-system monitor |
| `awfan profiles [--json]` | List discovered firmware profiles |
| `awfan presets` | Show known AC16251 profile names |
| `awfan doctor [--json]` | Check AWCC access and discovered resources |
| `awfan state [--json]` | Show awfan's remembered command state |
| `awfan clear-state` | Clear remembered command and RPM history |
| `awfan boost <cpu> <gpu> --yes` | Send raw manual fan-boost values |
| `awfan max --yes` | Send maximum boost to both fans |
| `awfan profile <0-5> --yes` | Select manual profile 0 or a discovered firmware profile |
| `awfan auto <1-5> --yes` | Select a discovered dynamic firmware profile |
| `awfan broker-status` | Check whether the elevated broker is reachable |
| `awfan update --check` | Check the latest stable GitHub release |
| `awfan update` | Download, verify, and install the latest stable release |
| `awfan update --force` | Reinstall the latest stable release |

Run `awfan help` for the complete built-in reference.

## Safety

- Start with conservative boost values.
- Keep `awfan watch 1` open while testing manual controls.
- Boost values are firmware inputs from 0 to 100; they are **not** direct fan percentages.
- A value of `80` drove the tested AC16251 close to maximum fan speed.
- Return to a firmware profile with `awfan auto <1-5> --yes` after manual testing.
- Do not assume profile IDs or fan IDs are identical on another model.

Read [Troubleshooting and safety](docs/TROUBLESHOOTING.md) before reporting a control issue.

## Build from source

Requirements:

- Windows 10 or 11 x64
- Visual Studio 2022 Build Tools with the C++ workload
- CMake 3.24 or newer

```powershell
git clone https://github.com/Gio1112/awfan.git
cd awfan
cmake -S native -B build/native -A x64
cmake --build build/native --config Release
.\build\native\Release\awfan.exe version
```

Install the locally built binaries and broker:

```powershell
.\install.ps1
```

Create the distributable ZIP:

```powershell
cmake --build build/native --config Release --target package
```

## Documentation

- [Compatibility](docs/COMPATIBILITY.md)
- [Troubleshooting and safety](docs/TROUBLESHOOTING.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Legacy PowerShell prototype](docs/LEGACY.md)
- [Changelog](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

## Project layout

```text
native/
  include/awfan/      Public native headers
  src/                Frontend, broker, core, and AWCC backend
  package/            Installer, updater, completion, notices, and package docs
.github/workflows/    Windows build and release automation
docs/                 Compatibility, architecture, and troubleshooting docs
src/                  Retired PowerShell prototype
```

The PowerShell implementation in `src/` and the root-level legacy files are retained for historical reference. New users should use the native release package.

## Contributing

Bug reports, compatibility results, documentation fixes, and carefully tested code contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening an issue or pull request.
