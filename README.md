<div align="center">

# awfan

**A lightweight native fan and thermal CLI for Alienware systems on Windows.**

[![Release](https://img.shields.io/github/v/release/Gio1112/awfan?display_name=tag&sort=semver)](https://github.com/Gio1112/awfan/releases/latest)
[![Windows build](https://github.com/Gio1112/awfan/actions/workflows/native-probe.yml/badge.svg)](https://github.com/Gio1112/awfan/actions/workflows/native-probe.yml)
[![Windows x64](https://img.shields.io/badge/platform-Windows%20x64-0078D4)](https://github.com/Gio1112/awfan/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-black.svg)](LICENSE)

</div>

awfan communicates directly with the Alienware Command Center WMI interface. The current native CLI is written in C++20 and does **not** require the external AlienFan CLI, Python, or PowerShell at runtime for fan and thermal commands.

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
- Verified self-updates from GitHub Releases
- Portable Windows package with installer, uninstaller, checksum, and PowerShell completion
- No background service or administrator-only installation required

## Compatibility

| System | Read support | Write support | Status |
| --- | --- | --- | --- |
| Alienware 16X Aurora AC16251 | Tested | Tested | Supported reference system |
| Other Alienware systems exposing `ROOT\WMI:AWCCWmiMethodFunction` | Possible | Untested | Community testing needed |

The tested AC16251 exposes two fans, CPU and GPU thermal sensors, and five firmware thermal profiles. Other systems may use different resource IDs or behavior.

See [Compatibility](docs/COMPATIBILITY.md) before testing another model.

## Install

### Recommended: release package

1. Download the `awfan-<version>-windows-x64.zip` package and matching `.sha256` file from the [latest release](https://github.com/Gio1112/awfan/releases/latest).
2. Extract the ZIP.
3. Open PowerShell in the extracted folder.
4. Run:

```powershell
.\install.ps1
```

Open a new terminal, then verify the installation:

```powershell
awfan version
awfan doctor
awfan status
```

The installer places awfan in `%LOCALAPPDATA%\Programs\awfan` and adds that directory to your user `PATH`. It does not require an elevated installation.

### Updating

Starting with awfan 1.0.1, future stable releases can be installed without downloading another ZIP manually:

```powershell
awfan update --check
awfan update
```

The updater downloads the latest stable package and matching SHA-256 checksum from GitHub Releases, verifies the package, waits for the current awfan process to exit, and then runs the packaged installer. Git and a repository checkout are not required.

Version 1.0.1 itself must be installed manually once because awfan 1.0.0 does not contain the updater command.

### Portable use

No installation is required:

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

Profile `0` is intentionally diagnostic-only because it did not reliably clear an existing manual boost during testing.

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
| `awfan profile <1-5> --yes` | Select a discovered firmware profile |
| `awfan auto <1-5> --yes` | Alias for `profile` |
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
.\build\native\Release\awfan.exe doctor
```

Install the locally built executable:

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
  src/                C++ AWCC backend and CLI
  package/            Installer, updater, completion, notices, and package docs
.github/workflows/    Windows build and release automation
docs/                 Compatibility, architecture, and troubleshooting docs
src/                  Retired PowerShell prototype
```

The PowerShell implementation in `src/` and the root-level legacy files are retained for historical reference. New users should use the native release package.

## Contributing

Bug reports, compatibility results, documentation fixes, and carefully tested code contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening an issue or pull request.

Security problems should be reported privately according to [SECURITY.md](SECURITY.md), not through a public issue.

## License and acknowledgements

awfan is released under the [MIT License](LICENSE).

Protocol research used public AWCC implementations as references. See [`native/package/THIRD-PARTY-NOTICES.txt`](native/package/THIRD-PARTY-NOTICES.txt) for attribution.
