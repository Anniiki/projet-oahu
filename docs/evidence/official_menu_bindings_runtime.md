# Official corroboration of the menu data services

## Result

The exclusive ui-vmf-menu-bindings profile read **14 ranges in the executable
section, 2 176 bytes, and three four-byte scalars**, that is **2 188 bytes**.
Every request is readable and equals the secondary PE studied with Rea.
Ten ranges also equal the official file on disk; the four host accesses
440AB0/440B00/440BA0/440E00 differ. The three scalars equal both files and
form TDU_Menus followed by three NULs. No cause for the differences between
disk and memory is inferred.

The executed copy is the official PC MC1.66A, SHA-256
3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30.
The secondary retains SHA-256
f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed.

## Exact coverage

| Nature | VA / sizes in bytes |
| --- | --- |
| Link, copy, integer write, integer read | 440AB0/78, 440B00/78, 440BA0/163, 440E00/166 |
| Account loading | 9AB470/177 |
| Row mask and loop | 9AB950/359, 9ABAC0/105 |
| Selected window of the loader 9ABC30 | 9AC127/24 |
| Resolution at the first ID | 9AE4D0/65, 9AE600/61 |
| Field conversion | 9AEB40/125, 9AEBC0/512, 9AEDC0/204 |
| Dispatch data in the executable section | 9AEE8C/59 |
| Topic name, three non-executable scalars | F05170/4, F05174/4, F05178/4 |

The body ranges gather **nine complete functions / 2 093 bytes** according
to the Rea AddressSets. The gaps 9ABAB7..BF (nine bytes) and 9AEBBD..BF
(three) are excluded. The 24-byte window does not cover all of 9ABC30;
the 59-byte table contains data, not instructions.

## Trial and verification

Slot 0 of coop-anything; private batch
`<isolated-copies>/slot0/20261031T170334-34228`.
PID 38644, FILETIME 134379398510306664, observed base 00400000.
The instrumented launcher owns a Job limited to one process, stopped at the
100.167-second timeout. Two windows are recorded, with no visual inspection
and no action in the menus. The temporary display bridge is borderless
D3D9On12/DXGI; it is not the final native D3D12 engine.

The before/after audits verify **6 697 reference files and 17 savegame
files unchanged**. The game documents are isolated and the slot released.
The reading probe follows no pointer and does not modify its target.
It verifies the path and records the process creation; the offline
verifier then cross-checks PID/FILETIME with the launcher reports.
The reader checks the pages' policy and their allocation base, without
publishing their exact protections or testing MEM_IMAGE separately.

Both C++latest x64/Win32 builds and the code probe self-test pass; that probe
is not published in this repository.
Static assertions bound size, types, sums and overlaps.
The wrapper accepts the new profile and retains its timing guards.
A zero return code from the probe is not enough: every readable entry
and the sizes/SHA of the captures are verified individually.

Private evidence:
`<private-evidence>/reprise-2026-10-31/vmf-menu-bindings-official-v1/`.
The verify-runtime.py verifier produces comparison.json, SHA-256
d6dcb8106982f1ea7c58d1db1a8a1a5a96043f252ff0ea36a411cbfb1e6beadb.
Its 32 digests cover the captures, reports, snapshots and commands.
The commercial bytes stay outside Git.

This evidence establishes the presence of the observed code. It does not
validate its execution, the whole sbdd service, playable menus,
authentication or co-op. The [data reconstruction](vmf_menu_data.md)
retains the service boundaries that remain to be ported.
