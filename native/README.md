# awfan native backend experiment

This directory contains the first C++20 replacement for the external AlienFan-CLI backend.

## Current scope

The `0.1.0-probe` build is deliberately **read-only**. It:

- connects to Windows WMI through COM;
- scans `ROOT\\WMI` and `ROOT\\CIMV2`;
- finds classes related to AWCC, Alienware, Dell, ACPI, fans, sensors and thermals;
- lists the methods exposed by matching classes;
- can optionally list method input and output parameters.

It cannot set fan speeds, change profiles or write hardware state.

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

Run the normal filtered probe first:

```powershell
.\\build\\native\\Release\\awfan-native.exe probe
```

Then collect method signatures:

```powershell
.\\build\\native\\Release\\awfan-native.exe probe --signatures
```

To inspect one namespace in full:

```powershell
.\\build\\native\\Release\\awfan-native.exe probe --namespace ROOT\\WMI --all
```

The output from the filtered `--signatures` run will tell us the exact Windows class, methods and payload shapes to implement next.

## Planned stages

1. Read-only class and method discovery.
2. Read-only sensor, RPM and power-profile queries.
3. Carefully gated fan-boost writes with side-by-side validation against AlienFan-CLI.
4. Replace the PowerShell broker with a native Windows service or restricted named-pipe broker.
5. Replace the remaining PowerShell client when the native backend is proven stable.
