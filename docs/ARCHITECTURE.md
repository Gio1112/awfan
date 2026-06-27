# Architecture

awfan is a native Windows command-line application built with C++20.

## Runtime layers

```text
Standard-user awfan.exe frontend
        |
        | named pipe, current-user ACL
        v
Elevated awfan-broker.exe scheduled task
        |
        | direct child process, no command shell
        v
Protected awfan-core.exe
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

## Frontend

`native/src/frontend_main.cpp` builds `awfan.exe`. It handles:

- Help and version output
- `broker-status`
- Stable release updates
- Local non-WMI commands
- Routing AWCC commands to the broker
- Direct core execution when the frontend itself is already elevated

The frontend is not privileged. It is the executable exposed through the user's `PATH`.

## Elevated broker

`native/src/broker.cpp` and `native/src/broker_main.cpp` build `awfan-broker.exe`.

The installer registers the broker as a per-user scheduled task with the highest run level. The executable is stored in `C:\Program Files\awfan`, so an ordinary user process cannot replace the privileged binary or the core executable it launches.

The broker:

- Uses a pipe name derived from the current Windows SID
- Rejects remote named-pipe clients
- Applies a pipe ACL for the current user, Administrators, and SYSTEM
- Accepts only an explicit allowlist of awfan hardware commands
- Passes arguments directly to `awfan-core.exe` without `cmd.exe` or another command shell
- Captures and relays stdout, stderr, and the process exit code
- Terminates a long-running core child when its client disconnects
- Uses a per-user mutex to prevent duplicate broker instances

The named pipe is a convenience boundary, not a security boundary between processes already running as the same Windows user. Any process running as that user can request an allowlisted awfan operation. Hardware-changing operations still require `--yes` in the core CLI.

## Core CLI

`native/src/cli_main.cpp` and the AWCC implementation sources build `awfan-core.exe`.

The core handles:

- Command parsing
- `--json` and `--yes` flags
- Input validation
- Fan and temperature reads
- Power-profile reads and writes
- Manual fan-boost writes
- JSON and human-readable rendering
- Local command-state persistence
- RPM trend calculation

`awfan-core.exe` does not elevate itself. It runs with the token supplied by either an elevated frontend or the broker.

## WMI protocol

The tested system exposes the `AWCCWmiMethodFunction` class under `ROOT\WMI`.

awfan discovers the active instance, prepares packed 32-bit method arguments, calls the WMI provider, and reads the 32-bit firmware response. Resource enumeration identifies fans, thermal sensors, and power profiles at runtime.

Low-level diagnostic implementations live in:

- `native/src/awcc_exact.cpp`
- `native/src/awcc_raw_v3.cpp`
- `native/src/awcc_inspect.cpp`
- `native/src/wmi_probe.cpp`

These diagnostics should remain read-only unless a future change has a separately reviewed safety case.

## Installation and updates

The broker-enabled installation uses:

```text
C:\Program Files\awfan
```

The installer requests one UAC approval, copies the protected binaries, registers the scheduled task, starts it, and adds the installation directory to the current user's `PATH`.

`-NoBroker` installations use `%LOCALAPPDATA%\Programs\awfan` by default and do not register an elevated task.

The updater downloads a release ZIP and matching SHA-256 checksum. The packaged installer then replaces the protected components and restarts the broker. Broker-enabled updates may request one UAC approval.

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

CMake builds three executables with the MSVC static runtime:

- `awfan.exe` — standard-user frontend
- `awfan-core.exe` — internal AWCC command implementation
- `awfan-broker.exe` — hidden elevated broker

CPack creates a Windows x64 ZIP containing the executables, installer, updater, uninstaller, completion script, license, changelog, README, and third-party notices.

The Windows workflow builds, smoke-tests the frontend and broker, packages, extracts, performs a broker-free CI install, executes, uninstalls, and hashes the package.

## Legacy implementation

The PowerShell prototype in `src/` used an external backend, elevated scheduled tasks, and a branch updater. It is not part of the native runtime. See [LEGACY.md](LEGACY.md).
