# Save-path witness of the observation layer

## Scope

The Win32 observation layer, a `dinput8.dll` proxy loaded by the official
executable, answers the game's Documents queries with an isolated folder and
guards the write opens made through `NtCreateFile` (see the
[observation mod](../mod-observation.md) page). Which Documents queries it
accepts, and the status it returns for a missing parent folder, are described
in [observation folder compatibility](observation_folder_compatibility.md).

The game is expected to keep its profile list and its saves in a folder
named `savegame`, under Documents or, as a fallback, under its own folder.
The save lines show, call by call, where such a folder or one of its entries
is opened for writing, and whether the guard admitted or refused the open.

The generic events cannot show this. `file-write-observed` and
`unconfined-write-refused` keep eight individual lines per process, which are
spent at startup, and a later summary names only the path of the call that
triggers it. Neither tells which save entry was created, replaced or refused.

This is instrumentation only. It adds no redirection and changes no decision
of the write guard: a save line is recorded once the guard has decided, after
the generic event of the same call. Like the rest of the layer, it is
infrastructure written for this project, a policy of the observation log, not
a reconstructed routine of the game.

## What the DLL records

The guard treats an open as a write when its access holds a write, delete or
security right (`GENERIC_WRITE`, `GENERIC_ALL`, `FILE_WRITE_DATA`,
`FILE_APPEND_DATA`, `FILE_WRITE_EA`, `FILE_WRITE_ATTRIBUTES`, `DELETE`,
`WRITE_DAC` or `WRITE_OWNER`), when its disposition is not `FILE_OPEN`, or
when its options include `FILE_DELETE_ON_CLOSE`. Recognized input-device and
system-service opens are forwarded before the guard. Every other write open
whose path has a `savegame` component is recorded under one of two dedicated
events, through `RecordSaveWrite` in `mod/src/observation_hooks.cpp`:

- `save-write-observed` for an admitted open. The decision is `admitted`.
  `error` is `0` when the native call returns a non-negative status, and
  otherwise that status as an unsigned decimal number: a name collision
  (`STATUS_OBJECT_NAME_COLLISION`, `0xC0000035`) is written as `3221225525`.
- `save-write-refused` for a refused open. `error` is the Win32 code that
  matches the status returned to the caller: `3` (`ERROR_PATH_NOT_FOUND`) for
  a missing parent folder, `161` (`ERROR_BAD_PATHNAME`) for a bare name
  without a root handle, `87` (`ERROR_INVALID_PARAMETER`) for an absolute
  name with a root handle, and `5` (`ERROR_ACCESS_DENIED`) otherwise. The
  decision is the refusal reason when the request has no canonical path (for
  example `parent-directory-missing`, `relative-name-without-root` or
  `noncanonical-or-reparse-file-path`), or the refusal event when the
  canonical path is protected (`source-write-refused`,
  `source-installation-write-refused` or `source-ancestor-change-refused`).
  The name forms behind these codes are described in
  [native path compatibility](observation_nt_name_forms.md).

A save line holds these fields, in this order:

- the fields common to every observer line: `project=TDU-Recompilation`,
  `pid`, `tid` and `tick` (milliseconds from `GetTickCount64`);
- `event`, then `error` in decimal;
- `sample` and `count_total`, both equal to the number of the line for this
  event in the process;
- `reason`, which holds the decision, then the request's `access`,
  `disposition` and `options` as `key=value` pairs, each value written as
  `0x` and eight hexadecimal digits;
- `path`, last, in UTF-8; it may contain spaces.

No caller address is recorded: the return address of this hook lies in the
system libraries, and the frames of the game are not read.

The path is the most resolved form available: the canonical path; else the
absolute path built from the request, a name relative to a root handle
included; else the name as requested. For a refused open it can be more
resolved than the path of the generic line, which then names the request as
made.

Each of the two events keeps up to 4096 individual lines per process
(`SaveLines`). Past that count, an open is still counted but gets no line of
its own. Such an open writes a summary line instead, `observation-summary`
with `source_event` naming the save event, only when it comes at least five
seconds after the first line of that event or after its previous summary.
The summary gives `count_total`, `count_since_last`, `nonzero_results` and
`empty_paths_total`, but the error, decision, request fields and path of that
one open only.

## Path component test

`tdur::mod::HasSaveComponent` (`mod/include/tdur/mod/save_path.hpp`) is a
`constexpr`, `noexcept` function of a `std::wstring_view`. The hook calls it
once per guarded write open, on the path described above. It returns true
when one component of the path is exactly `savegame`:

- the separators are `\` and `/`;
- only the ASCII letters `A` to `Z` are folded to lower case, so `SaveGame`
  and `SAVEGAME` match; any other character must be equal;
- a longer or shorter component never matches: `savegames`, `xsavegame`,
  `savegame.bak`, `savegame:stream` and `savegam` do not;
- a NUL character ends no component: the length of the view decides.

## Tests

None of these tests executes the commercial game: they run only programs
whose source code is in the repository.

- `observation_save_paths` (target `tdur_save_path_test`,
  `mod/tests/save_path_test.cpp`) checks 33 decisions at run time. Fifteen
  paths match, among them Win32, `\??\`, relative, forward-slash,
  mixed-separator and mixed-case forms. Sixteen do not, among them the empty
  path, a lone separator, a drive root, lookalike names, names with a space,
  a non-ASCII letter and an alternate stream suffix. Two views hold an
  embedded NUL. Two more decisions are checked by `static_assert` at compile
  time.
- `observation_proxy_and_documents` runs `mod/tests/run_fixture.py` on the
  witness program `tdur_mod_witness` (`mod/tests/observation_witness.cpp`),
  which loads the proxy DLL. Inside the isolated Documents folder of the run,
  the witness requests a save file under a missing `savegame` folder: through
  `CreateFileW`, then through `NtCreateFile` relative to a root handle, then
  ten times as a bare name without a root handle. It then creates the folder,
  creates it a second time, which fails with a name collision, creates twelve
  entries and a transient entry that it deletes, and creates lookalike names
  (`savegame.bak`, `xsavegame`, `savegames` and a file inside it). Last, it
  tries to write a save under the protected source Documents folder.
- `observation_cursor_installation` runs the same witness program with the
  cursor diagnostics enabled and applies the same save checks.

In both witness runs, the runner requires:

- exactly 13 `save-write-refused` and 15 `save-write-observed` lines, each
  checked for its path, error code and decision. The native requests are
  also checked for their exact access, disposition and options. The two
  `CreateFileW` requests are checked for their disposition (`FILE_CREATE` for
  `CREATE_NEW`, `FILE_OVERWRITE_IF` for `CREATE_ALWAYS`), for at least
  `GENERIC_WRITE` and `SYNCHRONIZE` in the access, and for the synchronous,
  non-directory options;
- both events numbered from 1 without a gap, no summary in their place and
  no lookalike name among them;
- the generic lines of the same calls: `nt-name-form-refused` for the first
  bare name and `source-write-refused` for the source save;
- on disk, the twelve entries and none of the refused or transient ones, the
  lookalike entries, and an empty source `savegame` folder.

## Limits

- The save lines have not been exercised with the game. No test establishes
  where the game writes its profile list or its saves.
- Only `NtCreateFile` is hooked for files. A deletion or a rename made
  through other system calls gets no line: the witness's transient entry is
  expected to leave one line, for its creation, and none for its deletion.
  An open through `NtOpenFile`, whatever its access, and the target of a
  rename or of a hard link are neither guarded nor recorded.
- A request for `MAXIMUM_ALLOWED` with the `FILE_OPEN` disposition, no
  explicit write right and no delete-on-close option is not treated as a
  write: it gets neither the generic line nor the save line.
- Past 4096 individual lines of one save event, a burst can end with no
  summary, since a summary needs a later open of the same event at least
  five seconds after the event's first line or its previous summary. No test
  reaches this limit: the witness program shows only that both events keep
  more than eight individual lines.
- The save lines depend on the folder name `savegame`. A save written under
  another name gets only the generic events.
- A request whose name is 1024 characters or longer gets only the generic
  refusal. A request that cannot be resolved to an absolute path, for example
  through a root handle that names no file, is classified on the name as
  requested, without the folders the root handle stands for. One whose
  absolute path cannot be made canonical is classified on that absolute path.
- An open made before the DLL is loaded, or through a direct system call, is
  not seen, and writes through a handle obtained earlier never reach
  `NtCreateFile`.
