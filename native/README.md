# awfan native backend experiment

This directory contains the first C++20 replacement for the external AlienFan-CLI backend.

## Current scope

The `0.2.1-probe` build is deliberately **read-only**. It:

- connects to Windows WMI through COM;
- scans `ROOT\\WMI` and `ROOT\\CIMV2`;
- finds classes related to AWCC, Alienware, Dell, ACPI, fans, sensors and thermals;
- lists the methods exposed by matching classes;
- optionally lists method input and output parameters;
- inspects AWCC class and method qualifiers;
- checks for provider instances without requiring them.

It cannot set fan speeds, change profiles, invoke AWCC methods or write hardware state.

## Build

Use a Visual Studio Developer PowerShell:

```powershell
cmake -S native -B build/native -A x64
cmake --build build/native --config Release
```

The executable will be at:

```text
build/native/Release/awfan-native.exe
```

## Test on the Alienware

Switch to the experiment branch and build:

```powershell
git switch codex/native-backend-probe
git pull
cmake -S native -B build/native -A x64
cmake --build build/native --config Release
```

Run the normal filtered probe:

```powershell
.\\build\\native\\Release\\awfan-native.exe probe
```

Collect method signatures:

```powershell
.\\build\\native\\Release\\awfan-native.exe probe --namespace ROOT\\WMI --signatures
```

Inspect AWCC class and method qualifiers without invoking anything:

```powershell
.\\build\\native\\Release\\awfan-native.exe inspect-awcc
```

## Confirmed on the Alienware 16X Aurora AC16251

The firmware exposes `AWCCWmiMethodFunction` in `ROOT\\WMI`. It exposes no enumerable instances, which is valid for a provider implemented through static class methods. The next probe confirms each method's `Static` qualifier and uses the class path `AWCCWmiMethodFunction` as the future `ExecMethod` target.

Relevant methods confirmed by the probe include:

- `GetFanSensors(uint32 arg2) -> uint32 argr`
- `Thermal_Information(uint32 arg2) -> uint32 argr`
- `Thermal_Control(uint32 arg2) -> uint32 argr`
- `SystemInformation(uint32 arg2) -> uint32 argr`
- `PowerInformation(uint32 arg2) -> uint32 argr`
- `TccControl(uint32 arg2) -> uint32 argr`

No method invocation is enabled in this branch yet.

## Planned stages

1. Read-only class, method and static-provider discovery.
2. Allowlisted read-only sensor, RPM and power-profile queries.
3. Carefully gated fan-boost writes with side-by-side validation against AlienFan-CLI.
4. Replace the PowerShell broker with a native Windows service or restricted named-pipe broker.
5. Replace the remaining PowerShell client when the native backend is proven stable.
