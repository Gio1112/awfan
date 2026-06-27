# Compatibility

awfan communicates with the Alienware Command Center WMI interface in `ROOT\WMI`. Compatibility depends on the methods and resources exposed by each system's firmware and Dell software stack.

## Tested system

| Property | Value |
| --- | --- |
| Model | Alienware 16X Aurora AC16251 |
| Operating system | Windows 11 x64 |
| AWCC class | `AWCCWmiMethodFunction` |
| Instance | `ACPI\PNP0C14\AWCC_0` |
| Fan IDs | `0x32`, `0x33` |
| Temperature IDs | `0x01`, `0x06` |
| Firmware profiles | `0xA0` through `0xA4` |
| Manual profile | `0x00` |
| Reported nominal maximum | 5400 RPM per fan |

Validated behavior includes:

- System-signature discovery
- Fan and temperature-resource discovery
- Fan-to-sensor mapping
- Current and nominal maximum RPM
- CPU and GPU temperatures
- Power-profile discovery and switching
- Manual fan-boost writes
- JSON output
- Live monitoring and RPM trends

## Known profile names

The tested machine uses these USTT profile mappings:

| CLI index | Raw ID | Name |
| ---: | ---: | --- |
| 1 | `0xA0` | Balanced |
| 2 | `0xA1` | Balanced Performance |
| 3 | `0xA2` | Cool |
| 4 | `0xA3` | Quiet |
| 5 | `0xA4` | Performance |

Do not assume the same order or names on another machine without checking the discovered IDs.

## Testing another Alienware model

Start with read-only commands:

```powershell
awfan version
awfan doctor --json
awfan status --json
awfan profiles --json
```

Do not begin with `boost`, `max`, `profile`, or `auto`.

A system is a promising candidate when:

- `awfan doctor` finds an AWCC instance
- At least one fan and one temperature sensor are discovered
- RPM and temperatures remain plausible across repeated reads
- The current power profile and available profile IDs can be read consistently

## Reporting compatibility

Open a compatibility issue and include:

- Exact Alienware model
- Windows version
- AWCC version, when installed
- `awfan version`
- Redacted output from `awfan doctor --json`
- Redacted output from `awfan status --json`
- Whether testing was read-only

Remove computer names, usernames, paths, service tags, serial numbers, and other identifying data.

## Unsupported or uncertain behavior

The following are not guaranteed across models:

- Fan resource ordering
- Sensor names and ordering
- Profile names
- Nominal maximum RPM
- Meaning and scaling of manual boost values
- Whether a profile switch fully clears manual fan control
- Whether AWCC methods require additional Dell services or drivers

Until a model has been tested, treat all write support as experimental.
