# Contributing to awfan

Thanks for helping improve awfan. Contributions are welcome, especially compatibility reports from Alienware systems that have not yet been tested.

## Before opening an issue

1. Install the latest release.
2. Run `awfan doctor`.
3. Run `awfan status --json`.
4. Check the existing issues for the same model or error.
5. Remove usernames, computer names, paths, serial numbers, service tags, and other personal data from logs before posting them.

For hardware-control problems, return to a known firmware profile before collecting more information:

```powershell
awfan profiles
awfan auto 1 --yes
```

Use the profile index appropriate for your system.

## Compatibility reports

A useful compatibility report includes:

- Exact Alienware model
- Windows version and architecture
- Whether Alienware Command Center is installed
- `awfan version`
- `awfan doctor --json`
- `awfan status --json`
- Which read-only commands work
- Whether any write command was tested

Do not test write commands merely to complete a report. Read-only results are valuable.

## Development setup

Requirements:

- Windows 10 or 11 x64
- Visual Studio 2022 Build Tools with Desktop development with C++
- CMake 3.24 or newer
- Git

Build the native CLI:

```powershell
git clone https://github.com/Gio1112/awfan.git
cd awfan
cmake -S native -B build/native -A x64
cmake --build build/native --config Release
```

Run the non-writing checks first:

```powershell
.\build\native\Release\awfan.exe version
.\build\native\Release\awfan.exe help
.\build\native\Release\awfan.exe doctor
.\build\native\Release\awfan.exe status --json
```

Create the package:

```powershell
cmake --build build/native --config Release --target package
```

## Pull requests

- Keep each pull request focused on one change.
- Explain the user-visible behavior and why it is needed.
- Include the Alienware model used for hardware testing.
- Keep read-only discovery separate from write-path changes when practical.
- Do not add a hardware write without validation, bounds checking, and an explicit confirmation guard.
- Update the README, CLI help, JSON schema notes, and changelog when behavior changes.
- Keep the executable buildable with the MSVC static runtime configuration in `native/CMakeLists.txt`.

## Hardware-write safety

Changes involving `Thermal_Control`, manual boost, profile changes, or other firmware writes need extra care:

- Never run an unknown write on a contributor's machine without clear consent.
- Prefer a read-only probe before implementing a write.
- Require explicit CLI confirmation for writes.
- Validate ranges before calling WMI.
- Provide a documented path back to firmware-controlled operation.
- Do not describe a raw firmware value as a percentage or target RPM unless the firmware behavior has been verified.

## Coding style

- C++20
- Four-space indentation
- Clear names over abbreviations
- RAII for COM and Windows resources
- No exceptions crossing the CLI boundary
- Human-readable errors with the original HRESULT when available
- Stable, versioned JSON fields for machine-readable output

## Documentation changes

Documentation-only pull requests are welcome. Keep commands copy-pasteable in PowerShell and distinguish clearly between:

- Read-only commands
- Experimental write commands
- Tested behavior
- Inferred or untested behavior

## Security

Do not report a vulnerability in a public issue. Follow [SECURITY.md](SECURITY.md).

By participating, you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
