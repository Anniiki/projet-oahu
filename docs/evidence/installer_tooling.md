# Windows installer: tooling and distribution contract

## November 3, 2026 update: tools package with CPack

`tools/release/package_devtools.py` now creates a ZIP with **CPack** from an
explicitly allowed staging inventory. The project has a tracked MIT license.
The October 30 findings below remain a historical survey: they no longer
describe the existence of the packager or license as of November 3.
The playable client and its final installer remain to be implemented.

The package contains five x64 executables: `tdur_inspect_game.exe`,
`tdur_inspect_bank.exe`, `tdur_inspect_database.exe`, `tdur_view_texture.exe`
and `tdur_view_mesh.exe`. It adds `LICENSE`, the project's English and French
README files, a usage guide and `manifest.json`. File selection does not
recursively traverse the game installation or build directory. Executables
are read and checked, never launched by CPack.

The schema 2 manifest records:

- `product: development-tools`, x64 architecture and Release configuration;
- `client_version`, read from `project(TDURecompilation VERSION ...)`, and
  the full source commit; this number does not establish a playable client;
- `protocol: {version: null, status: not-applicable}`: no network version
  is invented for these tools;
- `files`, the size and SHA-256 of the nine payload files other than the
  manifest itself, and `binaries`, the subset of five executables;
- the CTest summary and SHA-256 of the supplied report, the CPack version
  and ZIP generator;
- `provenance.build_source_binding: not-attested`: the packager checks the
  supplied files without rebuilding the tools or attesting their provenance.

`SHA256SUMS.txt` hashes the complete ZIP. Its embedded manifest must match
the external copy. The producer and `publish_release.py` use the same
validator: exact, unique names; every file covered by the manifest; sizes and
hashes; x64 PE headers; no embedded Windows profile path; nonnegative test
counts with at least one executed test and no failures. Documents are checked
too. A report with every test skipped is rejected. The PE check is neither
a publisher signature nor proof of origin.

This step requires Python 3.11 or newer, Git, CPack on PATH or an explicit
`--cpack` path, a clean source tree and the five already-built binaries.
The [build guide](../building.md) presets serve targeted validation; the
preset without GPU tests does not configure the viewers. Do not silently
alter an existing cache to add them. Adapt this packaging example to a
verified build containing all five tools:

```powershell
py -3 -I tools/release/package_devtools.py `
  --repo . --build-dir build `
  --output-dir 'D:/artifacts/oahu-tools-new' `
  --ctest-report 'D:/artifacts/ctest-x64.xml' `
  --cpack 'C:/Program Files/CMake/bin/cpack.exe'
```

The output directory must be new and outside the repository.
Spaces and Unicode are tested; `;`, `$`, `[` and `]`, and newlines are
rejected before output creation because CPack reinterprets them in its
installation scripts. Compression is limited to one thread. The local
`CPackConfig.cmake`, `cpack.log`, staging directory and internal CPack files
are not included in the distributed ZIP.

The [CPack configuration](https://cmake.org/cmake/help/v3.31/module/CPack.html)
uses `CPACK_INSTALLED_DIRECTORIES` for this staging directory only. The
[ZIP generator](https://cmake.org/cmake/help/v3.31/cpack_gen/archive.html)
requires no third-party installer. No global `install()` rule or packaging
target is added to the root CMakeLists.

Local validation: CPack `3.31.6-msvc6`, 13 Python tests at
BelowNormal priority. Initial counterexamples showed two missing refusals:
an entirely skipped report and a duplicate ZIP entry, even with a recomputed
external checksum. They pass after correction. Other cases cover altered
documents, incomplete manifests, an invented protocol, dirty sources,
existing output, invalid PE files, paths and CPack failure. These tests use
synthetic PE headers and a simulated GitHub API, without commercial assets.

Publication remains a separate command. A v1 manifest is not accepted by the
new validator; existing releases remain unchanged. Neither a checksum nor a
CTest report supplied afterward proves that the binaries derive from the
declared commit. An attested build pipeline, signatures, complete third-party
notices, clean-PC installation, client updates and rollback remain separate
steps.

## State verified on October 30, 2026

**No playable installer or complete client is available.** The current
executables are inspectors, viewers, observers and component tests. They do not
constitute a distributable co-op session. The milestone for notifying Anniiki
and the same setup for both PCs are defined in
[the final project goal](../final_project_goal.md#distribution-and-first-co-op-trial).

Reproducible private inventory:
`<private-evidence>/reprise-2026-10-30/installer-tooling/collect.ps1`,
`inventory.json`, four PE/import listings and their SHA-256. Observed revision:
`6865c4223e81f3a808071807d566c759ea4d2ade`. Only files from our own build were
examined. No setup, build, game, repair, elevation, registry change or firewall
change was executed; no `Win32_Product` query was made.

| Tool | Local finding | Consequence |
|---|---|---|
| CMake / CPack | `3.31.6-msvc6`, in the VS2022 Build Tools | `INNOSETUP`, `NSIS`, `NSIS64`, `WIX`, `ZIP` generators available. |
| Inno Setup / ISCC | Not found in PATH, standard folders or the targeted installed products | No local compiler whose version could be pinned; a CPack generator is not that compiler. |
| NSIS / makensis | Same result | CPack requires NSIS ≥ 3.03. |
| WiX / wix, candle, light | Same result, including the usual .NET locations | CPack produces an `.msi`; this alone does not provide the requested `.exe`. |
| SignTool | SDK `10.0.26100.0`, file version `4.00 (WinBuild.160101.0800)` | Tool present; no available certificate or project signature is attested. |
| dumpbin | `14.44.35229.0` | Imports and architecture of the four EXEs verified without running them. |
| IExpress | `11.00.26100.1` | Presence observed only; no validated update/uninstall contract. |

The search is bounded to the recorded locations, not to every portable tool on
the disk. No executable was downloaded.
References: [CPack NSIS](https://cmake.org/cmake/help/v3.31/cpack_gen/nsis.html),
[CPack WiX](https://cmake.org/cmake/help/v3.31/cpack_gen/wix.html),
[SDK SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).

## Proposed choice, still to be validated

Retain **CPack + Inno Setup**: a single `.exe`, per-user installation, prior
detection and an uninstaller. CPack 3.31 includes the generator (available
since 3.27), requires Inno ≥ 6 and accepts Pascal files through
`CPACK_INNOSETUP_CODE_FILES`. This allows a versioned recipe without shipping
the development tools in the package.
[Generator documentation](https://cmake.org/cmake/help/v3.31/cpack_gen/innosetup.html).

The official page recommends **Inno 7.1.1 x64**, released on 2026-10-28; it
also offers 6.7.4. Candidate choice: pin 7.1.1, its binary and its hash at the
time of its future acquisition, then validate compatibility with our CPack.
This combination has not been run here. No purchase is undertaken; the exact
license and notices of the acquired version will be archived.
[Official releases](https://jrsoftware.org/isdl.php),
[terms and the purchase question](https://jrsoftware.org/isorder.php#required).

The project currently has neither `install()` rules for our components nor
`include(CPack)`; `CMAKE_INSTALL_PREFIX` alone produces no distribution.
A positive list of Runtime components will be needed. CPack packages what the
install rules give it: never copy the whole `build/` or the project root. The
[CPack documentation](https://cmake.org/cmake/help/v3.31/module/CPack.html)
describes these rules and `CPACK_PACKAGE_CHECKSUM=SHA256`.

## Prerequisites actually observed

The four examined images are AMD64. Release projects are `v143`, SDK
`10.0.26100.0`, `/MD` (`MultiThreadedDLL`), recent C++ `/std:c++latest`.
Direct imports of the two viewers include `d3d12.dll`, `dxgi.dll`,
`D3DCOMPILER_47.dll`, `MSVCP140.dll`, `VCRUNTIME140.dll`,
`VCRUNTIME140_1.dll`, the UCRT and Windows API sets. The inspector adds `bcrypt`.
This survey does not establish the full closure of dependencies loaded on demand.

SDL **3.4.18 is statically linked**, with the source archive pinned in
`cmake/sdl3.cmake`; `sdl_gamepads_test.exe` does not import `SDL3.dll`, but keeps
the dynamic CRT and the Windows libraries. This linkage does not turn SDL into
an absent dependency: keep its notices and audit the backends actually
distributed. See [project dependencies](../dependencies.md).

On the developer PC, the VC v14 x64 and x86 runtimes are **14.51.36247.0**.
The local `vc_redist.x64.exe` / x86 carry **14.44.35211.0**; they were hashed,
never run. Their presence does not demonstrate the minimal runtime of a
release. An official x64 redistributable compatible with the toolset must be
pinned and a clean PC tested; do not ship DLLs taken from `System32` or a Debug
runtime. Microsoft requires a matching architecture and a sufficiently recent
version. [Supported VC runtime](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170).

Proposed deployment choice: per-user application, official central CRT as a
prerequisite, included offline and verified. If a sufficient version exists,
do not reinstall or downgrade it. Otherwise the prerequisite may request UAC;
its refusal must stop the installation cleanly. Uninstalling the client never
removes this shared runtime. The app-local mode is possible but would require
a separate recipe and maintenance; it is not validated here.
[Microsoft deployment and maintenance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170).

The current backend creates `IDXGIFactory6` and a D3D12 device at feature level
**11_0**. The first interface sets a Windows 10 **1803** API floor, without
proving our full compatibility with that version. The local Windows build is
26H2, `26300.9711`; a clean Windows/GPU matrix remains necessary. The final
setup must check the real device/driver, without masking a failure through WARP.
Vulkan remains to be implemented in this client; its requirements are not yet
established.
[Factory6 prerequisites](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_6/nn-dxgi1_6-idxgifactory6).
The Windows/Vulkan SDKs, VS, Python, Node, Rea and Rust must not be required
on player PCs to run the final client.

## Detection and data separation

Selecting an original installation must remain local to each PC: a path
proposed solely from the known `install_path`, in the relevant registry views,
then a folder picker. A registry value is not proof of version; never import a
serial number or an account.

Reuse the read-only check of `inspect_installation`:
`TestDriveUnlimited.exe`, **16,797,696 bytes**, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
It also checks the presence of `DB.bnk`, `Cameras.bin`, `AIConfig.xml`, but does
**not** check every resource. Before publication, complete a required, versioned
data profile; refuse an unknown base with a diagnostic, without patching,
repairing or modifying its protection. This identity check is not a license
check.
[Current contract](../../include/tdur/game/installation.hpp),
[implementation](../../src/game/installation.cpp).

Install our product outside this installation and the save games, for example
`%LOCALAPPDATA%\Programs\TDU-Recompilation`. Put config, caches, logs and our
own saves in a separate user space. Reject overlapping roots and uncontrolled
traversals/junctions. Commercial data is only ever opened read-only; any save
import works on a copy, without writing to the original saves.

## Building and identity of the same setup

Workflow to implement, **not a currently working procedure**:

1. Pin revision, toolset/SDK, CMake, ISCC, SDL sources and options; build
   Release x64 in a clean folder. Run the validations and produce a list of
   actually distributable components.
2. Create an empty Runtime staging with only the ready client/launcher, ready
   backends, default configuration files and notices. Exclude tests, viewers,
   probes, x86 observation DLLs, RE tools, analysis databases, commercial
   data, accounts, keys and saves. No complete client target exists yet to put
   in this staging.
3. Generate a manifest: product version, revision/build ID, architecture,
   exact dependency versions, config/save schema, data profile, protocol and
   established capabilities; for each component, relative path, size and
   SHA-256. A final protocol version must not be invented from
   `project(VERSION 0.1.0)`.
4. Configure CPack INNOSETUP, native x64 directives and `PrivilegesRequired=lowest`
   for our product, a stable AppId, real README/licenses. The prerequisite and
   publication steps must have explicit errors.
5. Produce the setup **once**, with its external SHA-256 and the validation
   evidence. The setup SHA stays external to avoid self-reference.

The [privileges](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm),
[architecture](https://jrsoftware.org/ishelp/topic_setup_architecturesallowed.htm)
and [AppId](https://jrsoftware.org/ishelp/topic_setup_appid.htm) directives are
documented by Inno. Emulated x64 on ARM64 is not validated: target `x64os` first.
A repeatable process does not yet prove a bit-for-bit identical build; compare
two clean builds before making that claim. A future Authenticode signature and
its timestamp are a separate step; a checksum is not a publisher signature.
No signature/certificate is created by this survey.

## Update, rollback and uninstall to be tested

Plan immutable `releases/<build-id>` versions and a stable launcher. Verify the
whole staging and the free space before publishing; do not replace running
binaries. Publish the current version only after success, with a transaction
log and the previous version kept. Cancellation, a full disk or a prerequisite
failure must leave the previous version launchable. This application-level
transaction is not provided by the choice of a generator.

Migrations keep customized bindings, config and saves: copy before conversion,
explicit schema, tested rollback. An older version must not silently open a
schema that has become incompatible. A downgrade requires a defined rollback
path, not a file overwrite.

Uninstallation removes only our manifested files, shortcuts and product
metadata. Keep user data by default and never delete the original game, the
shared runtime or data added by the player. Avoid any recursive wildcard
deletion; Inno specifically flags this risk in
[UninstallDelete](https://jrsoftware.org/ishelp/topic_uninstalldeletesection.htm).

Before the first shipment: two PCs/VMs without development tools, absent and
newer runtime, Unicode/space paths, compatible/unknown base, standard accounts,
cancellation, upgrade/downgrade, open application, lack of space, resume after
interruption and uninstall with personal data intact. Only then comes the
actual two-player session, with vehicle visibility/synchronization, transitions
and reconnection as foreseen by the milestone. **None of these
installation or co-op trials is declared successful by the present inventory.**

Finally, the SDL notices are present in the downloaded source; no general
project license was found by the survey of the tracked LICENSE/COPYING/NOTICE
names. The distribution terms of our components and their notices must be
settled before the first package; do not use the CPack sample license as the
client license.
