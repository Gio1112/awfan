# awfan native CLI

awfan 1.1.0 builds three Windows x64 executables:

- `awfan.exe` — public command-line frontend
- `awfan-core.exe` — internal AWCC implementation
- `awfan-broker.exe` — per-user background broker

## Build

```powershell
cmake -S native -B build/native -A x64
cmake --build build/native --config Release
```

Create the release package:

```powershell
cmake --build build/native --config Release --target package
```

## Installation

```powershell
.\install.ps1
```

The default installation places the binaries in `C:\Program Files\awfan`, registers the broker at sign-in, and adds the directory to the current user's `PATH`.

A broker-free install is available for portable use and CI:

```powershell
.\install.ps1 -NoBroker
```

## Commands

```powershell
awfan broker-status
awfan doctor
awfan status
awfan fans
awfan temps once
awfan watch 2
awfan profiles
awfan presets
awfan state
awfan update --check
```

Hardware writes remain experimental and require `--yes`:

```powershell
awfan boost 20 20 --yes
awfan max --yes
awfan profile 1 --yes
awfan auto 1 --yes
```

Boost values are raw firmware inputs, not target percentages or target RPM values.

## Known AC16251 profile IDs

| Index | Raw ID | Name |
| ---: | ---: | --- |
| 1 | `0xA0` | Balanced |
| 2 | `0xA1` | Balanced Performance |
| 3 | `0xA2` | Cool |
| 4 | `0xA3` | Quiet |
| 5 | `0xA4` | Performance |

The broker accepts only the supported awfan hardware commands and launches the protected core executable directly. Output and exit status are relayed to the normal terminal.
