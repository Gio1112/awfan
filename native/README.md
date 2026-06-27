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
.\build\native\Release\awfan.exe state --json
```

Install the protected background broker and frontend:

```powershell
.\install.ps1
```

Create the Windows x64 ZIP:

```powershell
cmake --build build/native --config Release --target package
```

awfan 1.1.0 adds a per-user elevated broker so ordinary terminals can access the AWCC provider without repeated UAC prompts.
