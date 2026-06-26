# awfan native CLI

The native application is now documented in [`CLI.md`](CLI.md).

Build:

```powershell
cmake -S native -B build/native -A x64
cmake --build build/native --config Release
```

Run:

```powershell
.\build\native\Release\awfan.exe doctor
.\build\native\Release\awfan.exe status
```

Create the portable Windows ZIP:

```powershell
cmake --build build/native --config Release --target package
```

The stable `main` branch remains untouched while the native release candidate is validated on `codex/native-backend-probe`.
