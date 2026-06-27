# awfan native CLI

The native application is documented in [`CLI.md`](CLI.md).

Build:

```powershell
cmake -S native -B build/native -A x64
cmake --build build/native --config Release
```

The Release build creates:

```text
build\native\Release\awfan.exe
build\native\Release\awfan-core.exe
build\native\Release\awfan-broker.exe
```

Run the public frontend:

```powershell
.\build\native\Release\awfan.exe version
.\build\native\Release\awfan.exe help
```

Install:

```powershell
.\install.ps1
```

Create the Windows x64 ZIP:

```powershell
cmake --build build/native --config Release --target package
```

awfan 1.2.0 adds friendly mode names, restoration timers, custom presets, broker tools, redacted reports, and update recovery. See [`../docs/PLATFORM-1.2.md`](../docs/PLATFORM-1.2.md).
