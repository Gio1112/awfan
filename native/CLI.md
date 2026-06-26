# awfan native CLI

`awfan 1.0.0-rc1` is a standalone C++20 Windows CLI that communicates directly with the Alienware AWCC WMI provider. It does not call AlienFan CLI, Python, PowerShell, or the Alienware Command Center application at runtime.

## Build

```powershell
cmake -S native -B build/native -A x64
cmake --build build/native --config Release
```

The executable is created at:

```text
build/native/Release/awfan.exe
```

Create the portable ZIP with:

```powershell
cmake --build build/native --config Release --target package
```

## Commands

Read-only commands:

```powershell
awfan status
awfan status --json
awfan fans
awfan temps once
awfan temps 2
awfan watch 2
awfan profiles
awfan presets
awfan doctor
awfan state
```

Experimental control commands:

```powershell
awfan boost 20 20 --yes
awfan max --yes
awfan profile 1 --yes
awfan auto 1 --yes
```

Every hardware-changing command requires `--yes`.

Boost values are firmware fan-boost inputs from 0 to 100. They are not target fan percentages and are not target RPM values. Testing on the AC16251 showed that a value of 80 drove both fans close to maximum speed, so the CLI reports the raw commanded boost value and actual RPM trend instead of inventing a target RPM.

A boost command enters manual control. Use `profile` or `auto` with one of the discovered indexes from 1 to 5 to clear the boost and return to dynamic firmware control. Firmware profile 0 remains visible in diagnostics but is intentionally not accepted as a profile command because it did not reliably clear an existing boost on the tested system.

## Known USTT profile names

For the discovered AC16251 IDs:

| Index | Raw ID | Name |
| ---: | ---: | --- |
| 1 | `0xA0` | Balanced |
| 2 | `0xA1` | Balanced Performance |
| 3 | `0xA2` | Cool |
| 4 | `0xA3` | Quiet |
| 5 | `0xA4` | Performance |

The raw profile ID remains the source of truth. Run `awfan profiles` and `awfan presets` before changing profiles.

## RPM trends

`status`, `fans`, and `watch` compare consecutive samples and report whether each fan is rising, falling, or stable. The output includes RPM delta and RPM per second when enough sample history exists.

The firmware-reported maximum RPM is treated as nominal. Live telemetry can exceed that value, so utilization over 100% is displayed rather than silently clamped.

## Local state

The CLI stores only its own last command and previous RPM sample in:

```text
%LOCALAPPDATA%\awfan\state-v1.txt
```

Clear it with:

```powershell
awfan clear-state
```

## Packaging

The ZIP contains:

- `awfan.exe`
- `install.ps1`
- `uninstall.ps1`
- `awfan-completion.ps1`
- `README.txt`
- `CHANGELOG.txt`
- `THIRD-PARTY-NOTICES.txt`

`install.ps1` installs to `%LOCALAPPDATA%\Programs\awfan` and adds that directory to the current user's PATH. Administrator access is not required for installation.

CI verifies the executable, package contents, extracted package executable, temporary install/uninstall flow, and SHA-256 checksum generation.

## Confirmed AC16251 resources

- AWCC instance: `ACPI\PNP0C14\AWCC_0`
- Fan IDs: `0x32`, `0x33`
- Temperature sensor IDs: `0x01`, `0x06`
- Power profile IDs: `0xA0` through `0xA4`
- Manual profile ID: `0x00`
- Nominal fan maximum: 5400 RPM

The stable `main` branch remains untouched while this release candidate is validated on `codex/native-backend-probe`.
