# Notification helpers in the official process

On 31/10/2026, an isolated trial of 100.227 seconds launched the official
MC1.66A copy via the launcher and the workspace slot using coop-anything.
The primary executable retains its SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
No menu action, remote invocation, or protection modification
was performed.

## Bounded reading

The exclusive `ui-vmf-child-helpers` profile of `tdur_probe_code` reads two
complete bodies: 451C90/115B and 416450/82B. Four scalars of 4 bytes
cover the imports 9EC104, 9EC36C, 9EC378 and the suffix F04DC8, totaling 213 bytes
at most. Pages must belong to the main image and have the
required type. No imported pointer is followed by the reader.

The 197 bytes of code correspond to the bodies analyzed with Rea on the
secondary reference. They differ from the primary on disk; no cause
for this difference is inferred. The suffix word is A7000000 and
corresponds to both files on disk.

A separate diagnostic checks the process creation identity, collects
module metadata via Toolhelp, then compares each imported pointer
to the exact address of an export from its module. DUMPBIN reads only
the corresponding DLL files; their checksums are preserved.

| Fixed address | Loaded module export |
| --- | --- |
| 9EC104 | KERNEL32.DLL: GetLocalTime |
| 9EC36C | MSVCR71.dll: strncpy |
| 9EC378 | MSVCR71.dll: sprintf |

These aliases are confirmed in memory. The import table of the primary on
disk places its entries elsewhere; this difference is recorded without
inferring any loader behavior.

## Results and limits

Process 27380 is identified by its creation FILETIME
134379594842719359. The 6 697 reference files and 17 player savegames
remain unchanged; the slot and its single owned process are released.
Paths, commands, hashes, exports, and comparisons are preserved outside
Git in `<private-evidence>/reprise-2026-10-31/vmf-child-helpers-official-v1/`.

The reader passes its own checks in x64 and Win32. The bytes present
in memory do not prove targeted routine execution. The graphics bridge
for this trial remains borderless D3D9On12/DXGI; it validates neither the final
native D3D12 renderer, nor playable menus, nor multiplayer or co-op.
