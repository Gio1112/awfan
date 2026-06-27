# awfan native CLI

`awfan 1.0.0` is a standalone C++20 Windows CLI for Alienware AWCC fan and thermal control. It communicates directly with the AWCC WMI provider and does not require AlienFan CLI at runtime.

## Build

```powershell
cmake -S native -B build/native -A x64
cmake --build build/native --config Release
```

The executable is created at `build/native/Release/awfan.exe`.

Create the portable ZIP with:

```powershell
cmake --build build/native --config Release --target package
```

## Main commands

```powershell
awfan status
awfan status --json
awfan fans
awfan temps once
awfan watch 2
awfan profiles
awfan presets
awfan doctor
awfan state
```

Experimental control commands require `--yes`:

```powershell
awfan boost 20 20 --yes
awfan max --yes
awfan profile 1 --yes
awfan auto 1 --yes
```

Boost values are raw firmware fan-boost inputs, not target percentages or target RPM values. Use a firmware profile from 1 to 5 to return from manual boost control to dynamic firmware control.

## Known profile IDs

| Index | Raw ID | Name |
| ---: | ---: | --- |
| 1 | `0xA0` | Balanced |
| 2 | `0xA1` | Balanced Performance |
| 3 | `0xA2` | Cool |
| 4 | `0xA3` | Quiet |
| 5 | `0xA4` | Performance |

Profile 0 is diagnostic-only because it did not reliably clear an existing boost on the tested machine.

## State and telemetry

awfan stores its last command and previous RPM sample at `%LOCALAPPDATA%\awfan\state-v1.txt`. Clear it with `awfan clear-state`.

`status`, `fans`, and `watch` compare consecutive samples and report RPM trend, delta, and RPM per second. The firmware maximum RPM is treated as nominal because live telemetry can briefly exceed it.

## Packaging

The ZIP contains:

- `awfan.exe`
- Install and uninstall scripts
- PowerShell completion
- Package README and changelog
- MIT license
- Third-party notices

The installer uses `%LOCALAPPDATA%\Programs\awfan` and updates the current user's PATH.

awfan 1.0.0 was validated on the Alienware 16X Aurora AC16251.
