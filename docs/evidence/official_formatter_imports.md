# CRT imports of the notification formatter

## Reference and reading

Primary reference: PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The `ui-vmf-formatter-imports` profile reads only two non-executable DWORDs:
`9EC358` and `9EC370`, eight bytes in total. It follows no pointer and reads
no function body. The process identity, section and size checks remain those
of the read-only reader.

The isolated trial of 01/11/2026, PID 33992, FILETIME identity
`134379735540703350`, lasted 100.185 seconds. The module inventory of that
same process and the exports of the loaded file establish:

| Cell | Module | Export RVA | Name |
| --- | --- | --- | --- |
| `9EC358` | `MSVCR71.dll` | `11E08` | `atoi` |
| `9EC370` | `MSVCR71.dll` | `2C224` | `sscanf` |

Module SHA-256:
`8094af5ee310714caebccaeee7769ffb08048503ba478b879edfef5f1a24fefe`.
The match covers the exact address of each export. It confirms the binding
of both cells; no targeted call to the CRT or to the game is executed.

## Validation and limits

The reader's self-tests pass in Release x64 and Win32. The comparator checks
the eight bytes, the identities, the before/after snapshots and the hashes of
the frozen sources. The 6697 references and 17 verified saves are
unchanged; the coop-anything workspace slot is released. Private evidence is
kept in
`<private-evidence>/reprise-2026-10-31/vmf-formatter-imports-official-v1/`,
notably `comparison.json` and `probe-validation.json`.

This observation proves neither the behavior of every CRT case nor the
execution of the formatting paths. The D3D9On12/DXGI bridge used for the
trial remains temporary; it validates neither the future native D3D12
renderer, nor the reconstructed menus, nor online connection or co-op.

## Named-parameter comparator

The separate `ui-vmf-named-import` profile reads only the non-executable
DWORD `9EC374`, four bytes in total. The next isolated trial,
PID 34008 / FILETIME `134379752028878773`, lasts 100.294 seconds and
establishes an exact match with the `strncmp` export, RVA `2955`, of the same
`MSVCR71.dll` file. This is also the name of the secondary import at that
cell. The name comparison is case-sensitive; no locale conversion or
`_strnicmp` comparison is inferred from its use.

The self-tests pass in x64 and Win32; snapshots, 6 697 references and
17 verified saves are unchanged, and the slot is released. The evidence
is kept separately in
`<private-evidence>/reprise-2026-10-31/vmf-named-import-official-v1/`.
This profile follows no pointer, calls no game function and does not
establish the execution of the lookup path.

## Numeric helpers of the percentage

The `ui-vmf-format-numeric` profile reads two complete bodies: `40C1A0`,
20 bytes, and `674EB4`, 117 bytes. It also reads the non-executable DWORD
`9EC34C`, **141 bytes in total**, without following its value as a pointer.
Both loaded bodies match the Rea results for the secondary reference; their
bytes differ from the primary on disk.

The isolated trial of 01/11/2026, PID 33516, lasts 100.211 seconds. The
metadata of the same process and the exports of the verified module confirm
`floor` at cell `9EC34C`. The self-tests pass in x64 and Win32; snapshots,
6 697 references and 17 verified saves are preserved, slot released.
Private evidence is kept in
`<private-evidence>/reprise-2026-10-31/vmf-format-numeric-official-v1/`.

This reading corroborates the code and the binding, without a targeted
invocation of these two functions. The x86 computation check program runs
separately: it replays an instruction sequence and calls the CRT export,
then compares the obtained values with the future C++ port.
