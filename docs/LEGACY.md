# Legacy PowerShell prototype

The repository contains the original PowerShell-based awfan prototype for historical reference:

- `src/awfan.ps1`
- `src/awfan-broker.ps1`
- `src/awfan-updater.ps1`
- `bin/awfan.cmd`
- `update-manifest.json`

That implementation wrapped an external `alienfan-cli.exe`, installed elevated scheduled tasks, and used a branch-based updater. It is no longer the supported public release.

## Supported implementation

awfan 1.x is the native C++20 application under `native/`. Public releases package `awfan.exe` with a per-user installer and do not require:

- The external AlienFan CLI
- The legacy elevated broker
- The legacy scheduled updater
- A GitHub access token

Use the release package documented in the root [README](../README.md).

## Source-checkout installer

The root `install.ps1` now installs only a locally built native executable from:

```text
build\native\Release\awfan.exe
```

It does not install the retired PowerShell broker or updater.

## Support policy

Issues and pull requests for the legacy implementation may be closed unless they are needed for migration, historical documentation, or removal of a security problem.
