# Tutorial triggering: 7FBEF0

`VmfTutorialServiceContext` reconstructs the service that calls the type2
message. It uses the owned catalog, the real VMF target resolution and
`VmfHelpMessageContext`. No x86 function pointer is executed.

## Order and retained data

A NULL tag ends the call before any read of the caller or of the globals.
Otherwise:

1. Read byte caller+929, then retain the initial manager.
2. Read its display, resolve `/avert`, then read its active state.
3. Read byte global 1416155; any non-zero value blocks the rest.
4. Retain the DB container, read the signed count then the row pointer.
   Test NULL rows before the non-positive count.
5. Compare keys by exact C-string equality, case-sensitive, rows in native
   order, stride 40.
6. For EACH matching row, reread the DB global at 7FBFAD and look up the
   sentinel in its catalog. The rows remain those of the initial container.
   Skip a row only if its text pointer equals the sentinel's; identical
   content under another ID remains admissible.
7. Call the presentation on the initial manager: type2, topic11, text in
   ARG9, flag10=1; the other arguments are zero.

A NULL container or manager would be dereferenced by the native code: the
port returns an explicit error. This differs from the normal branches for a
NULL display, NULL rows and a non-positive count. An unknown binding stays
pending. Row storage is shared and retained; the other bindings must remain
alive for the lifetime of the context, with serialised accesses.

## Metadata and resumption

`read_tutorial_sentinel_id(game_root)` requires the audited official SHA,
checks 17 bytes at VA 7FBFB3 / file 3FBFB3, the opcodes and the CALL target
9AE600. The high DWORD is zero after the successful strcmp. No default ID is
injected. This immutable metadata and the bindings belong to the same image
for the whole lifetime of the context.

The budget is shared with the nested presentation. A wait keeps the manager,
rows and catalog already selected; a resumption or a move does not replay
completed effects. A fault makes the context non-re-executable.
`message_invocations` counts the creation of the logical call context, before
its possible nested steps. `message_call_finished` guarantees neither a
found target nor displayed pixels.

## Validation of October 31, 2026

MSVC Release, C++latest, `/W4 /WX`, x64 and Win32: sixteen targeted suites
pass per architecture. The service passes 496 checks; the existing
presentation 840. The cases cover ordered guards, sentinel by identity,
reread of the global catalog, retained sources, move, budget and errors.

The official diagnostic reads the banks without modifying them: 92 rows,
562 catalog entries, 32 routes per VMF profile PC 2.8 / Xbox 2.5. Its 64
cases, 40 expected presentations and 7242 checks pass per architecture.
The display and the initial states of this diagnostic are fixtures; the
texts come from the real data, without recording them in Git or in the logs.

```powershell
.\build\Release\vmf_help_message_test.exe --tutorial-service
.\build\Release\vmf_help_message_test.exe --official-tutorial-service "<installation>"
```

## Native evidence and limits

Rea, the PE sections and DUMPBIN cross-check two complete bodies: 7FBEF0 /
282 bytes and 441120 / 96. The first equals the primary on disk; the second
differs from it. An exclusive probe without scalars finds the 378 studied
bytes in the isolated official process. No cause for the differences is
inferred, and no targeted execution of the routines is claimed.

The accompanying runtime trial lasts 100.166 s: 6697 references and 17 saves
are preserved, and the slot is released. The temporary bridge is
D3D9On12/DXGI borderless, not the final native D3D12 renderer. Private
evidence:
`<private-evidence>/reprise-2026-10-31/vmf-tutorial-service-{runtime,official}-v1/`.

An independent review read eleven sources (84289 bytes) without reporting a
concrete defect; a second review verified the hashes and bounded its verdict.
The two complete contracts proposed earlier are refused with documented
corrections. The manager constructor and global binding, higher-level transitions,
media loading, receiver and FSCommand remain to be wired. No usable
commercial menu, online profile or two-client co-op trial is validated by
this batch.
