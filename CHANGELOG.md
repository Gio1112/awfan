# Changelog

All notable public releases are documented here.

## 1.0.1 — 2026-06-27

### Added

- `awfan update --check` for checking the latest stable GitHub release
- `awfan update` for downloading, verifying, and installing the latest stable release
- `awfan update --force` for reinstalling the latest stable release
- SHA-256 verification before installing an update
- Automatic extraction and unblocking of verified release packages

### Changed

- The updater now uses public GitHub Releases instead of Git branches or private access tokens.
- Future stable updates no longer require Git or a local repository checkout.
- The package installer now includes `update.ps1`.

### Notes

- Version 1.0.1 must be installed manually once because 1.0.0 does not contain the updater command.
- AWCC WMI access may still require an elevated terminal on some systems.

## 1.0.0 — 2026-06-26

### Added

- Native C++20 AWCC WMI backend with no AlienFan CLI runtime dependency
- Live fan RPM, nominal maximum RPM, temperatures, and fan-to-sensor mappings
- Human-readable and versioned JSON output
- Continuous `watch` and temperature modes
- Measured RPM trend, delta, and RPM-per-second reporting
- Firmware-profile discovery and known AC16251 USTT profile names
- Experimental manual fan-boost and profile controls protected by `--yes`
- `doctor`, `state`, and `clear-state` commands
- Portable Windows x64 ZIP
- User-level installer and uninstaller
- PowerShell completion
- SHA-256 release checksum
- Windows CI packaging and installer smoke tests
- MIT license and third-party attribution
- Public compatibility, troubleshooting, architecture, security, and contribution documentation
- Structured bug, compatibility, and feature-request forms

### Safety behavior

- Manual boost values are reported as raw firmware inputs, not percentages or target RPM values.
- Profile `0` is diagnostic-only because it did not reliably clear an existing manual boost on the tested AC16251.
- Profiles `1` through `5` clear remembered manual boost values and return control to firmware.
- The retired PowerShell updater and private-token setup are no longer part of the supported installation path.

### Known limitations

- Direct firmware fan-percentage reads return `-2` on the tested system.
- Nominal RPM utilization is calculated from current RPM divided by the reported nominal maximum.
- Live RPM may briefly exceed the firmware's reported nominal maximum.
- Write support has been validated only on the Alienware 16X Aurora AC16251.
