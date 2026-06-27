# awfan 1.2 platform features

awfan 1.2 expands the native CLI around safer daily use, diagnostics, and maintenance.

## Firmware modes

```powershell
awfan mode
awfan mode --json
awfan mode cool --yes
awfan balanced --yes
awfan balanced-performance --yes
awfan quiet --yes
awfan performance --yes
```

The tested AC16251 mappings are:

| Index | Raw ID | Name |
| ---: | ---: | --- |
| 1 | `0xA0` | Balanced |
| 2 | `0xA1` | Balanced Performance |
| 3 | `0xA2` | Cool |
| 4 | `0xA3` | Quiet |
| 5 | `0xA4` | Performance |

Public status and profile output use these names while retaining the raw IDs.

## Restore and temporary control

Before entering manual control, awfan remembers the active firmware mode:

```powershell
awfan boost 55 55 --yes
awfan restore --yes
```

Temporary control can restore automatically:

```powershell
awfan boost 55 55 --yes --for 20m
awfan max --yes --for 5m
awfan boost 45 50 --yes --until-reboot
```

Supported duration suffixes are `s`, `m`, `h`, and `d`, up to seven days. The broker owns the timer, so closing a terminal does not cancel it. Reboot-scoped restoration distinguishes a Windows restart from a normal broker restart.

## Custom presets

```powershell
awfan preset create gaming 70 70
awfan preset list
awfan preset gaming --yes
awfan preset gaming --yes --for 30m
awfan preset delete gaming
```

Presets are stored in `%LOCALAPPDATA%\awfan\presets.json`.

## Broker management

```powershell
awfan broker status
awfan broker status --json
awfan broker restart
awfan broker repair
awfan broker logs
awfan broker logs 100
```

Status includes the broker PID and uptime, frontend/core/broker versions, task registration, and pipe reachability.

## Diagnostic reports

```powershell
awfan report
awfan report .\awfan-report.json
```

Reports include Windows version, component versions, broker health, AWCC discovery, telemetry, profiles, local state, and recent broker activity. Computer names, usernames, and user-profile paths are redacted automatically. Reports should still be reviewed before public sharing.

## Update recovery

The 1.2 updater verifies the published SHA-256 checksum, creates a local backup, installs the package, then validates the installed version, broker, and AWCC provider. A failed validation triggers restoration of the previous package.

This recovery applies to updates launched from 1.2 and later. The update into 1.2 is performed by the previously installed updater.
