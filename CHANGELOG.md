# Changelog

All notable public releases are documented here.

## 1.1.0 — 2026-06-27

### Added

- Native per-user elevated background broker
- Automatic broker startup at Windows sign-in through Task Scheduler
- Secure local named-pipe IPC restricted to the current user, Administrators, and SYSTEM
- `awfan broker-status` for checking broker availability
- Protected `awfan-core.exe` and `awfan-broker.exe` components installed in `C:\Program Files\awfan`
- Broker restart and start-when-available task settings
- Optional `-NoBroker` installation mode for portable use and CI

### Changed

- Normal terminals can now use AWCC status, monitoring, profile, and fan-control commands without repeated UAC prompts.
- Hardware commands are forwarded to the elevated broker, which executes only an explicit command allowlist.
- The broker launches `awfan-core.exe` directly without a command shell and relays output and exit status.
- The installer requests one administrator approval and migrates older per-user installations to the protected Program Files location.
- Updates may request one administrator approval while replacing and restarting the broker.
- Release publishing is now driven by the repository `VERSION` file rather than a hardcoded version.

### Security

- Privileged binaries are stored in an administrator-writable directory rather than `%LOCALAPPDATA%`.
- Remote named-pipe clients are rejected.
- The broker pipe ACL is scoped to the installing Windows user, Administrators, and SYSTEM.
- Every hardware write continues to require `--yes`.

### Known limitations

- The broker cannot run while Windows itself is asleep; it continues after resume.
- Write support remains validated only on the Alienware 16X Aurora AC16251.

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
