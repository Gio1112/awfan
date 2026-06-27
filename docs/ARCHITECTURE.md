# Architecture

awfan is a native Windows command-line application built with C++20.

## Runtime layers

```text
CLI parsing and presentation
        |
        v
Native awfan command layer
        |
        v
AWCC WMI backend using COM / IWbemServices
        |
        v
ROOT\WMI:AWCCWmiMethodFunction
        |
        v
Alienware firmware and thermal controller
```

## CLI entrypoint

`native/src/cli_main.cpp` handles:

- Command parsing
- `--json` and `--yes` flags
- Input validation
- Help and version output
- Routing to read, control, state, and diagnostic operations

Hardware-changing commands are rejected unless `--yes` is present.

## Native command layer

`native/src/native_cli_release.cpp` provides:

- AWCC connection and resource discovery
- Status snapshots
- Fan and temperature reads
- Power-profile reads and writes
- Manual fan-boost writes
- JSON and human-readable rendering
- Local command-state persistence
- RPM trend calculation

awfan stores only its own last command and previous RPM samples. It does not treat the local state file as firmware truth.

## WMI protocol

The tested system exposes the `AWCCWmiMethodFunction` class under `ROOT\WMI`.

awfan discovers the active instance, prepares packed 32-bit method arguments, calls the WMI provider, and reads the 32-bit firmware response. Resource enumeration identifies fans, thermal sensors, and power profiles at runtime.

Low-level diagnostic implementations live in:

- `native/src/awcc_exact.cpp`
- `native/src/awcc_raw_v3.cpp`
- `native/src/awcc_inspect.cpp`
- `native/src/wmi_probe.cpp`

These diagnostics should remain read-only unless a future change has a separately reviewed safety case.

## State

The native CLI stores its local state at:

```text
%LOCALAPPDATA%\awfan\state-v1.txt
```

The file may contain:

- Last control mode requested through awfan
- Last manual boost values
- Last requested profile
- Previous RPM samples and timestamps

Deleting the file does not change the current firmware state.

## Packaging

CMake builds `awfan.exe` with the MSVC static runtime. CPack creates a Windows x64 ZIP containing:

- The executable
- Per-user installer and uninstaller
- PowerShell completion
- Package README
- Changelog
- Third-party notices

The Windows workflow builds, smoke-tests, packages, extracts, installs, executes, uninstalls, and hashes the package.

## Legacy implementation

The PowerShell prototype in `src/` used an external backend, elevated scheduled tasks, and a branch updater. It is not part of the native runtime. See [LEGACY.md](LEGACY.md).
