# awfan native CLI

The native application is documented in [`CLI.md`](CLI.md).

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

The 1.0.0 release is packaged for Windows x64 and validated on the Alienware 16X Aurora AC16251.
