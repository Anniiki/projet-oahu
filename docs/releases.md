# Development releases

Validated subsystem milestones produce development snapshots on
[GitHub Releases](https://github.com/Anniiki/projet-oahu/releases).
Each snapshot identifies its exact public source commit, shipped tools,
test scope, remaining work and SHA-256 checksums.

## Automatic publication

The Windows workflow builds and tests x64 and Win32 on functional source,
test, CMake or release-tool changes pushed to main. Pull requests receive
validation without publication. Manual workflow dispatch can validate or
publish a milestone. Documentation-only edits do not create snapshots.

Publication runs only after both architecture jobs succeed. Its token has
write access to this repository's contents; build jobs use read access.
Actions are pinned to verified commits. A rerun preserves an existing
release for the same source commit instead of replacing downloaded files.

GPU-dependent suites are excluded on hosted runners. Hardware and WARP
checks remain separate local validations and must be reported accurately.
The release manifest records the x64 CPU test report used for packaging.

## Contents

The Windows x64 ZIP allowlists five project tools: installation, bank and
database inspection, plus native D3D12 texture and mesh viewers. It includes
the MIT license, guides, manifest and checksums. No commercial executable,
asset, save or product key is included.

Snapshots currently serve development and inspection. Full gameplay,
authenticated multiplayer, two-PC co-op and the final installer remain
under reconstruction. A playable preview must meet the
co-op readiness criteria.
