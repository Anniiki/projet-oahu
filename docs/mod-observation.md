# Win32 observation of the official executable

This module prepares diagnostics for official PC MC 1.66 A. It belongs
to TDU-Recompilation and does not depend on OpenTDU. The validations
below execute only witnesses whose source code is present. They prove
neither game startup, D3D12 rendering nor working co-op.

## Reference and provenance

The official x86 PE analyzed by Rea has SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Local project evidence `rea/original/02-inspect_pe_imports.json` identifies
`DirectInput8Create`, along with Shell32 imports `SHGetFolderPathA`,
`SHFileOperationA`, `ShellExecuteA` and `ShellExecuteExA`.

`mod/src/dinput8_proxy.cpp`, `observation_hooks.cpp` and the witnesses
are infrastructure code written for this project: no game routine is
reconstructed in these files. Microsoft Detours 4.0.1 is retained
unmodified in `mod/src/vendor/detours`, at commit
`1dbd4ea281d73d478aa8d09a8b83fbd265f715f7`. `PROVENANCE.json` contains
file checksums, with the original MIT license.

The method applies local coop-anything playbooks: proxy DLL, private
copies, process slot, save locking and checksum comparison. This module
does not yet install a network bridge or synchronization mechanism.

## Loading and contract

The x86 DLL must be named exactly `dinput8.dll`. Its sole export,
`DirectInput8Create`, occupies ordinal 1 and preserves the stdcall ABI.
The system DLL is loaded by absolute path from `GetSystemDirectoryW`
on the first factory call; its DirectInput object and HRESULT are
actually forwarded.

Hooks are installed in `DllMain`. A static import makes them available
before the EXE's global constructors. The launcher also uses
[DetourCreateProcessWithDllExW](https://github.com/microsoft/Detours/wiki/DetourCreateProcessWithDllEx),
which adds the DLL as the first import in the suspended process.
No on-disk executable file is patched.

Configure the environment **before** process creation:

| Variable | Value |
| --- | --- |
| `TDUR_PRIVATE_DOCUMENTS` | Existing, canonical private directory, representable in the process ANSI code page and shorter than `MAX_PATH`. |
| `TDUR_SOURCE_DOCUMENTS` | Existing original Documents root, protected against writes. |
| `TDUR_SOURCE_INSTALLATION` | Protected original installation; optional, but valid and existing if set. |
| `TDUR_OBSERVATION_LOG` | Log file beneath the private directory. |

Invalid configuration makes DLL initialization fail: no fallback to
the owner's Documents. No global redirection, registry key or ACL is changed.

## Diagnostic isolation

The effective addresses of `SHGetFolderPathA/W` and
`SHGetKnownFolderPath` are intercepted: imported calls and calls obtained
through `GetProcAddress` return the private directory for Documents.
Requests for another user token are rejected.

Write opens through `NtCreateFile` are canonicalized. An open under
original Documents, outside the private root, or under the original
installation is rejected and logged. Handle-relative paths and native
volume names are resolved. Unresolved paths and paths crossing a reparse
point are rejected;
[OBJ_DONT_REPARSE](https://learn.microsoft.com/en-us/windows/win32/api/ntdef/ns-ntdef-_object_attributes)
is also applied to the actual open.

Native HID interfaces recognized by GUID and resolved by `QueryDosDevice`
to a simple device object retain their exact `NtCreateFile` call,
including the keyboard suffix. Numeric `KeyboardClass` and `PointerClass`
objects are also forwarded. They never pass through CWD-relative path
resolution; a filename resembling an HID remains subject to file protections.

The log retains eight samples of each frequent event, then a summary
at most every five seconds: total, count since the last summary,
nonzero returns and empty logged paths. This also covers generic
`unconfined-write-refused` rejections, with general reason and original
native name when available. Rejections affecting source Documents/the
source installation, protected ancestors and hook initialization remain
detailed. Totals are periodic snapshots; the final partial interval
is not necessarily written during forced shutdown.

The external launcher keeps original saves open read-only, without
write or deletion sharing, then compares their inventory and SHA-256.
It assigns the suspended process to a non-breakaway Job limited to one
process before resumption authorized by the gameslot wrapper.
`ShellExecute`, `WinExec` and usual `CreateProcess` families are explicitly rejected.

These rejections deliberately limit this initial diagnostic: a secondary
launcher, a device requesting a noncanonical open or a protection action
may be blocked. Record the restriction in that case; it does not demonstrate
a game defect. No protection or activation bypass is undertaken.
This observation module is not a sandbox against hostile code, direct
system calls or write handles acquired before it loads.

## Build and evidence

From the repository, with MSVC 2022 and CMake:

```powershell
cmake -S mod -B build-mod-observation -G "Visual Studio 17 2022" -A Win32
cmake --build build-mod-observation --config Release --parallel 3
ctest --test-dir build-mod-observation -C Release --output-on-failure
```

Validation on 30/10/2026: MSVC 19.44.35229,
`/W4 /WX /std:c++latest`, four successful tests. Latest mode accepts
the available draft without promising complete C++26. The normal DLL
is in `build-mod-observation/bin/normal/Release/`; the launcher is in
`build-mod-observation/launcher/Release/`.

- The witness checks four private writes before `main`, then twenty
  more across multiple threads, DirectInput HRESULTs and child rejection.
- Seventeen source canaries remain identical; absolute, extended,
  `..`-containing and handle-relative paths create no new source file.
  The installation witness also remains identical.
- A mutant deliberately disables Documents rejection: the test detects
  the improper source creation. This DLL remains solely in `mutant-source`.
- The launcher witness validates injection before resumption, PID
  recording, deadline shutdown and checksums. Its scenarios remain fixtures.
- Eighteen classification checks distinguish native input interfaces
  from file and volume paths without opening a device.
- A burst of 100001 Documents calls and 100001 native empty-name opens
  preserves expected returns; counters are checked and the log stays
  below 256 KiB (12256 bytes in the 30/10/2026 validation).

Every run creates new evidence under `fixture-normal`, `fixture-mutant`
or `launcher-fixture`: `result.json`/launcher report and logs. Witness
manifests pin their checksums and those of the DLLs; `LastTest.log`
retains CTest results. Never launch the commercial game directly from
these tests: use the gameslot wrapper and a prepared game copy.
