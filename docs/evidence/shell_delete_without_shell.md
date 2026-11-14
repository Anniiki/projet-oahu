# Save folder deletion without the Shell

## Status

**New policy of the Win32 observation layer, not a recovered routine of the
game.** No code of the official executable is reproduced; only its observed
call is described. The module replaces one Windows Shell call by
infrastructure written for this project. It is compiled in every
observation DLL, with or without the gamepad bridge (`TDUR_MOD_GAMEPAD`).

## Problem

When a new game is started, the official PC MC 1.66 A executable calls
`SHELL32!SHFileOperationA` from its main thread with `wFunc` = `FO_DELETE`,
`fFlags` = `0x0414` (`FOF_SILENT`, `FOF_NOCONFIRMATION`, `FOF_NOERRORUI`) and
`pFrom` = `<private Documents>\Test Drive Unlimited\savegame\<active
profile>` followed by a double NUL. In a session analysed from two memory
dumps, the deletion completed, then the call waited without time limit on a
global critical section of `windows.storage.dll`, inside the Shell. The
observation layer was not involved in that wait. The cause of the abandoned
lock is not known; this module does not depend on it.

## Policy

The DLL detours `SHFileOperationA` and `SHFileOperationW`
(`mod/src/shell_delete.cpp`). A request is **handled** by the DLL only when
all of the following hold, checked in this order:

1. `lpFileOp` is not null;
2. `wFunc` is `FO_DELETE` (3);
3. `fFlags` holds `FOF_NOCONFIRMATION` (0x10) and no bit outside
   `FOF_SILENT` (0x4), `FOF_NOCONFIRMATION`, `FOF_SIMPLEPROGRESS` (0x100),
   `FOF_NOCONFIRMMKDIR` (0x200), `FOF_NOERRORUI` (0x400) and
   `FOF_NOCOPYSECURITYATTRIBS` (0x800). The DLL shows no user interface and
   implements neither the recycle bin (`FOF_ALLOWUNDO`), nor name mappings,
   nor connected elements, nor wildcard options; without
   `FOF_NOCONFIRMATION` the Shell would ask for a confirmation. `0x0414`
   passes;
4. the `pFrom` list is read up to its double NUL, 32,768 characters at most
   including the terminators. An empty list, a list not terminated within
   that bound, a path of `MAX_PATH` (260) characters or more, or a path
   holding `*` or `?` is forwarded. An empty string ends the list, as for
   the Shell, so an empty first entry is an empty list;
5. for `SHFileOperationA`, every path converts with
   `MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS)`; the wildcard rule is
   applied again on the converted path;
6. every path is accepted by `TdurPrivateSaveTarget`
   (`mod/src/observation_hooks.cpp`): the same canonicalization as the
   `NtCreateFile` write guard (`GetFullPathNameW`, drive-letter form only,
   every existing component free of reparse points, existing parents, long
   names) followed by a prefix comparison with
   `<TDUR_PRIVATE_DOCUMENTS>\Test Drive Unlimited\savegame`. The path must be
   **strictly under** that root: the root itself is refused;
7. no canonical path names a stream: a colon anywhere but right after the
   drive letter forwards the request (cause `stream`). A named stream under
   the root may belong, through a hard link, to a file outside it.

One refused path forwards the whole request, a mixed list included. Any
other request is forwarded to the original function with its arguments
unchanged. A handled request is deleted path by path, in list order, by the
engine below, on the calling thread: no Shell, no COM, no copy engine
handler, no shortcut list. `pTo`, `hwnd` and `lpszProgressTitle` are ignored,
as the documentation describes for `FO_DELETE` without user interface;
`hNameMappings` is not touched; `fAnyOperationsAborted` is set to `FALSE`.

The engine runs inside the "internal operation" scope of the write guard
(`TdurEnterInternalOperation`). Its opens go through the guard's
`NtCreateFile` hook, which passes them unchanged inside that scope; without
it, the guard would refuse to open a leaf reparse point that the engine must
delete as a link, and would turn the engine's opens relative to a held
directory into absolute ones. The engine checks every object itself, as
described next. Because of that scope, no `save-write-observed` line is
written for the engine's opens: the `shell-delete-handled` line is the trace
of a handled request.

The same scope lifts the guard's refusals for the source Documents, the
source installation and their ancestors. Installation therefore refuses a
save root that is equal to, or above, the source Documents or the source
installation (`TdurPrivateSaveRoot`); the DLL then refuses to load.

## Engine

`mod/src/shell_delete_core.cpp`, no dependency on Detours or on the journal;
`NtCreateFile` and `RtlNtStatusToDosError` are taken from `ntdll` at run time.
Input: the canonical target path and the save root.

1. A target path that names a stream is refused with `ERROR_INVALID_NAME`
   before anything is opened.
2. The root is opened by path with `FILE_LIST_DIRECTORY |
   FILE_READ_ATTRIBUTES | SYNCHRONIZE`, `FILE_FLAG_BACKUP_SEMANTICS |
   FILE_FLAG_OPEN_REPARSE_POINT` and an anonymous security quality of service
   (`SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS`, which only matters for a
   named pipe). It must be a directory and not a reparse point (attributes
   read on the handle, `FileAttributeTagInfo`); otherwise the request fails
   with `ERROR_ACCESS_DENIED`. Its final path is read on the handle with
   `GetFinalPathNameByHandleW(FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)`; it
   must equal the path the root was named by (case-insensitive) and name no
   stream: a folder above the root replaced by a junction would move the
   root elsewhere, and the request then fails with `ERROR_ACCESS_DENIED`.
   The handle stays open until the end.
3. The target is opened by its `\\?\` path with `FILE_READ_ATTRIBUTES` and
   the same flags, only to read where it really is and what it is (final
   path and attributes on the handle); that handle is closed at once. An
   absent target yields `ERROR_FILE_NOT_FOUND` or `ERROR_PATH_NOT_FOUND` and
   nothing is done. The final path must name no stream
   (`ERROR_INVALID_NAME`), must **equal the path requested**
   (case-insensitive) and must lie strictly under the root's final path;
   otherwise `ERROR_ACCESS_DENIED`. Equality refuses a target reached
   through a junction, whether it leads outside the root or to another
   folder of the root: the object behind it is never deleted in place of the
   one named.
4. Every directory between the root and the target is then opened as in
   step 5, relative to the handle of the previous one (the root's for the
   first), with `FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE`;
   it must be a plain directory, not a reparse point, and is held open until
   the end. The target is then opened the same way, as an entry of the last
   directory held.
5. Every object is opened **relative to the handle of its parent**:
   `NtCreateFile` with `OBJECT_ATTRIBUTES.RootDirectory` set to that handle,
   the entry name alone, `OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE`,
   disposition `FILE_OPEN`, options `FILE_OPEN_REPARSE_POINT |
   FILE_SYNCHRONOUS_IO_NONALERT`, plus `FILE_DIRECTORY_FILE` or
   `FILE_NON_DIRECTORY_FILE` as the parent's listing (or the probe, for the
   target) shows the entry, so that an entry that changed kind since fails
   to open. No other component is parsed by name and no reparse point is
   followed. A name that is empty, `.` or `..`, or holds a backslash or a
   colon, is refused with `ERROR_INVALID_NAME`. Then, on the object's own
   handle: its final path must equal the final path of the parent plus the
   entry name (case-insensitive), name no stream, and lie strictly under the
   root's final path (case-insensitive ordinal prefix followed by a
   separator). Otherwise the object is not touched and `ERROR_ACCESS_DENIED`
   (`ERROR_INVALID_NAME` for a stream) is recorded.
6. An object with more than one name (`FileStandardInfo.NumberOfLinks`
   above 1) is neither deleted nor changed, and `ERROR_ACCESS_DENIED` is
   recorded: its other names may lie outside the root and share its data,
   attributes, times and streams. The rest of the request continues.
7. An object that is a directory and not a reparse point (attributes read on
   its handle, never taken from a directory listing) is listed **through its
   handle** (`GetFileInformationByHandleEx`, `FileFullDirectoryRestartInfo`
   then `FileFullDirectoryInfo`, 64 KiB heap buffer). `ERROR_NO_MORE_FILES`
   ends the listing, and so does `ERROR_FILE_NOT_FOUND` on the first query (a
   file system without `.` and `..` answers so for an empty directory). The
   whole listing is read first; each entry (`.` and `..` excluded) is then
   opened as in step 5 and goes through steps 6 to 9. A directory is deleted
   only once all its entries are gone.
8. A reparse point (junction, symbolic link, mount point) is deleted as a
   link: it is only ever opened with `FILE_OPEN_REPARSE_POINT` (with
   `FILE_FLAG_OPEN_REPARSE_POINT` for the probe), never listed, and what it
   points to is never opened.
9. An object is opened with `DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE`,
   plus `FILE_LIST_DIRECTORY` for a plain directory (as the listing shows
   it). Deletion goes through the object's handle:
   `SetFileInformationByHandle(FileDispositionInfoEx,
   FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
   FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE)`. A read-only file or
   directory is deleted without its attribute changing. When the volume
   refuses that form (`ERROR_INVALID_PARAMETER` or `ERROR_NOT_SUPPORTED`),
   `FileDispositionInfo` is used; for a read-only object, the attribute is
   first cleared through a **second handle**, the only one that asks for
   `FILE_WRITE_ATTRIBUTES`, opened relative to the parent as in step 5 and
   checked again (same final path, same object by volume serial number and
   file index, still a single name), and restored if the deletion then
   fails. The handle is closed right after, which removes the name at once
   with the POSIX form, so that the parent can be deleted next.
10. Files and directories deleted are counted (a directory reparse point
    counts as a directory). The engine continues after the failure of one
    entry and keeps the first error met.

The root, the directories on the way to the target and the directories of the
current branch are held with `FILE_LIST_DIRECTORY`, and the object being
deleted with `DELETE`; all of them are opened with `FILE_SHARE_READ |
FILE_SHARE_WRITE`, without `FILE_SHARE_DELETE`. Both accesses enter the
sharing check, which an open limited to attributes does not: while the engine
holds these handles, no other handle can obtain the `DELETE` access that
renaming, moving or deleting those objects requires. A directory 200 levels
or more below the target fails with `ERROR_FILENAME_EXCED_RANGE`, nothing
below it being deleted. Listing buffers are on the heap; one shared buffer of
32,768 characters receives every final path.

What this guarantees: every deleted object, the target included, is opened
relative to the handle of a directory verified under the root and held open,
through a chain of held directories up to the held root, and it is itself
verified on its own handle as that very entry, under the root, free of
streams and with a single name, before its deletion; none of these
directories can be renamed or moved by another handle in between. No reparse
point is traversed, no object that has another name is deleted or changed,
and no stream is deleted on its own. The engine never acts on a path read
again after a check: the check and the deletion use the same handle.

## Value returned to the caller

- 0 when every path was entirely deleted;
- otherwise the first code met, all non-zero: `ERROR_FILE_NOT_FOUND` (2) or
  `ERROR_PATH_NOT_FOUND` (3) for an absent target, `ERROR_ACCESS_DENIED` (5)
  for a refused check or an object with more than one name, the Win32 code of
  the failure otherwise (sharing violation, directory not empty...). The
  other paths of the list are still processed.

These are Win32 codes, not the `DE_*` values that `SHFileOperation`
documents for some cases. The exact codes the Shell returns were not
measured systematically. One observation only: the process witness run
against a DLL without these hooks (an earlier, unpublished build) let the real `SHFileOperationA` handle an absent profile
folder under the synthetic root with `0x0414`, and it returned 2, the value
the engine returns too. How the game uses the value is not established.

A list that names the same path twice, or a folder and one of its
descendants, returns 2 although everything was deleted: the later path is
absent when its turn comes.

## Journal

One line per interception through `TdurObservationRecord`, outside the
sampled frequent events:

- `shell-delete-handled`: `error` = value returned; reason
  `handled api=A|W files=N dirs=M ms=T flags=0x%08lx apartment=<apartment>
  paths=K`; `path=` the canonical paths separated by `|`;
- `shell-delete-forwarded`, written **before** the original call so that a
  call that never returns leaves its trace: `error=0`; reason `<cause>
  api=A|W func=%u flags=0x%08lx apartment=<apartment>`, cause among
  `null-request`, `not-delete`, `flags`, `list-invalid`, `wildcard`,
  `path-too-long`, `ansi-conversion`, `outside-root`, `stream`; `path=` the
  requested paths separated by `|` (empty for an unreadable list);
- `shell-delete-forwarded-returned`, after the original call: `error` =
  value returned, same reason followed by `ms=T`.

`<apartment>` is the COM apartment of the calling thread read with
`CoGetApartmentType`: `sta`, `mta`, `na`, `mainsta`, or `none` when COM is
not initialized on that thread. Each line carries `caller_image` and
`caller_rva`, the return address of the call relative to the main image.
The path field is bounded to 600 UTF-16 units, followed by `...` when cut.
Installation writes `shell-delete-hooks-installed`, or
`shell-delete-hooks-install-failed`: with `error=5` and reason
`save-root-refused` when the save root holds the source Documents or the
source installation, with the Detours code otherwise. Either failure makes
the whole DLL refuse to load.

## Evidence

Component test `observation_shell_delete_core`
(`mod/tests/shell_delete_core_test.cpp`, x86): list reading for A and W (one
and several paths, empty and null lists, empty entry ending the list,
unterminated list, the exact 32,768-character boundary, 259 and 260
character paths, wildcards), every flag bit, the operation rule, the prefix
rule, the stream rule, the end of a listing; the engine on synthetic trees
created by the test under the build tree:

- a 50-level tree; read-only files and directories, with both disposition
  forms;
- a junction to an outside folder inside the tree (junction deleted and never
  listed, outside file and its attributes intact), and a target that is
  itself a junction (deleted as a link, the folder behind intact);
- a hard link to an outside read-only file, with both disposition forms: the
  link is refused (5) and kept, the single-name read-only file beside it is
  deleted, and the outside file keeps its data, attributes and times
  (creation, last access, last write, change);
- a named stream requested on such a hard link (`link:stream`): refused with
  `ERROR_INVALID_NAME`, nothing deleted, the outside file keeps its stream,
  data, attributes and times;
- symbolic links when the system allows their creation (the test prints
  which); absent target and absent parent; file target; outside target and
  the root itself (refused, nothing deleted);
- a target reached through a junction to an outside folder, and a target
  named through a junction to another folder of the root (both refused,
  nothing deleted behind the junction);
- a root named through a junction, and a root named below a junction with
  the target named either below the same junction or by its real path (all
  refused, nothing deleted); a target two levels below the root (deleted,
  its parents kept);
- a held file (sharing violation reported, the rest deleted);
- a directory swapped for a junction to an outside folder after its parent
  was listed (deleted as a link, outside file intact);
- a directory that the test tries to move away right after the engine listed
  it (the move is refused while the engine holds it, the deletion completes);
  the same move with the sharing protection lifted by a test option: the
  directory is moved and replaced by a junction to an outside folder holding
  an entry of the same name, held open by the test; the entry reached
  relative to the handle is the one of the moved directory, refused on its
  own handle with `ERROR_ACCESS_DENIED`, and the outside entry is never
  opened (an open by path would meet its sharing violation);
- once the root and the directories down to the target's parent are held,
  renaming that parent and renaming the root both fail, and the deletion
  completes;
- a file whose access list grants only `READ_CONTROL`, `SYNCHRONIZE` and
  `FILE_READ_ATTRIBUTES`: it refuses reading and is deleted through the
  folder's rights;
- the depth limit.

Before these checks were met, the same component test run against an
earlier, unpublished version of the engine (handles of the root and of the chain limited to
attributes, entries reopened by path, no stream or link-count refusal)
failed where expected: the stream request deleted the outside file's stream
and changed its times; the hard link was deleted and the outside file's
times changed; the entry open crossed the junction put in place of
the listed directory and met the decoy's sharing violation (32); the target's
parent could be renamed while the engine worked (renaming the root failed in
that run too, the reason was not measured); the target named through a
junction inside the root deleted the object behind it; the restricted file
failed with 5.

Process witness `observation_shell_delete_fixture`
(`mod/tests/shell_delete_witness.cpp`, `mod/tests/run_shell_delete_fixture.py`):
the witness imports the observation DLL statically and calls the two APIs on
trees prepared by the launcher under a private Documents folder of its own.
Handled: profile folders through `SHFileOperationA` and `SHFileOperationW`, a
relative path with the current directory under the root, a path through
`..`, an 8.3 short name when the volume has them (the launcher prints when
it does not), a tree holding a junction to an outside folder, read-only
entries, an absent profile (`2`), calls from an STA and an MTA thread. For
each: returned value, `fAnyOperationsAborted` = 0, exact journal line,
folder gone, outside junction target and an unrelated save folder intact.
Forwarded, last, each on its own thread awaited 10 s: outside path, mixed
list, wildcard, `FOF_ALLOWUNDO`, `FO_COPY`, a named stream under the root and
the root itself, each on paths that do not exist except the root; the
`forwarded` line with its exact cause is required, the `returned` line when
the original call returned. The junction case also shows, in the DLL, that
the engine's relative opens pass the `NtCreateFile` guard unchanged inside
the internal scope. Two more runs of the witness, with the source Documents
equal to the save root and with the source installation under it, must end
with `STATUS_DLL_INIT_FAILED` (`0xC0000142`) and the `save-root-refused`
installation line, before any case runs; the launcher sets
`SEM_FAILCRITICALERRORS`, so no dialog is shown. Against an earlier,
unpublished DLL,
the stream request was handled instead of forwarded and both runs loaded the
DLL.

A list of mutants, one per decision branch (flags, operation, wildcards,
mixed list, strictly under the root, check on the handle, root named by its
path, held directories, held chain, reparse points neither followed nor
listed, relative opens, probe equal to the request, streams in the decision
and in the engine, read-only, link count, reduced access, end of a listing,
depth, internal scope, installation check), each naming the test of these
two that should kill it, was written for this module; replaying them is a
separate step.

## Limits

- Component and witness tests prove the module on synthetic trees in a
  process of the project. They prove nothing about the game: an in-game
  trial must show that the new-game path no longer freezes, that the value
  returned suits the game and that the profile folder is created again by
  the next save. One game session has since shown the first point: at New
  Game the log recorded `shell-delete-handled` with `api=A`,
  `flags=0x00000414`, `error=0`, `files=1` and `dirs=2`, and the game went
  on. The other two are not established yet.
- Only the drive-letter form is handled; UNC paths, `\\?\` paths (they hold
  `?`), device paths, paths through reparse points and paths that name a
  stream are forwarded to the Shell, which keeps its behavior there.
- The Shell's return codes are not reproduced on purpose; one absent-path
  case agrees (2), partial failures were not compared. See above.
- `FILE_DISPOSITION_FLAG_POSIX_SEMANTICS` and
  `FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE` need a recent Windows 10
  or later and a volume that supports them; the fallback is exercised by the
  component test through a test option, not on a volume that refuses the
  extended form. The first-query `ERROR_FILE_NOT_FOUND` rule is checked as a
  pure decision only: NTFS always lists `.` and `..`.
- On the build machine, unprivileged symbolic links could
  not be created (error 1314): the symbolic link case printed that it was
  skipped. Junctions and hard links were tested.
- In the recorded runs, the forwarded cases returned within their 10 s
  bound; the witness still ends itself when one does not.
- An object held by another process with `DELETE` access (shared for
  deletion) makes the engine's open fail with a sharing violation (32),
  since the engine does not share deletion; the Shell might delete it.
- Folders above the root are not held by the engine. If one of them were
  renamed during a deletion, the final paths read afterwards would no longer
  match the root's final path and the objects would be refused
  (`ERROR_ACCESS_DENIED`): the failure is safe, the deletion incomplete. This
  case is not tested; NTFS is expected to refuse renaming a directory while a
  handle below it is open.
- The root must be reachable under the very path it was configured with: a
  private Documents folder on a substituted drive letter or a mapped network
  drive makes its final path differ, and every handled request then fails
  with `ERROR_ACCESS_DENIED`, nothing deleted and nothing forwarded. This
  follows from the code and was not tested.
- A target whose parent folder does not exist is refused by the
  canonicalization of the write guard and forwarded to the Shell.
- A failure to install these hooks, the refusal of the save root included,
  makes the whole observation DLL refuse to load, not only this module.
