# Crash and hang diagnostics

The Win32 observation launcher can write a minidump when the process it
starts crashes or stops responding, and it records each event in its report.
The intended target is the official x86 PC MC 1.66 A executable; every test
below runs source-owned witnesses instead. This is diagnostic
infrastructure, not a reconstructed game routine. The crash handler detours
one system function, `UnhandledExceptionFilter`, and the heartbeat reuses
the Present hooks of the display bridge. Loading, isolation and the other
report fields are described in
[Win32 observation of the official executable](../mod-observation.md) and
[launcher exit observations](../launcher_exit_observation.md).

## Components

In the observation DLL (`dinput8.dll`):

- `mod/src/crash_handler.cpp` maps the channel prepared by the launcher and
  detours `UnhandledExceptionFilter` in `kernelbase.dll`. `DllMain` installs
  it after the observation hooks and before the display gate; a failed
  installation makes DLL initialization fail. It creates no thread.
- `mod/include/tdur/mod/present_heartbeat.hpp` holds a pointer to a counter
  in the channel, incremented by the Present hooks of the display bridge.
- `mod/include/tdur/mod/crash_channel.hpp` defines the shared layout, the
  object names and the time limits used by both sides.

In the launcher (`tdur_observe_launch`, `mod/launcher`):

- `observe_launch.cpp` creates the channel, serves crash requests, samples
  the window of the child, runs the dump helper, asks the question, stops a
  hung child and writes the report sections described below.
- `watchdog_core.hpp` and `watchdog_core.cpp` hold every hang decision in a
  state machine without any Windows call, fed one sample at a time.
- `dump_helper.cpp` builds `tdur_dump_helper.exe`, an out-of-process
  minidump writer started once for a self-check, then once per dump. The
  build writes it next to the launcher, which refuses to start without it.

`tools/minidump_summary.py` reads the resulting dumps without dbghelp.

The window operation diagnostics of the same DLL (`window_diagnostics.hpp`,
enabled with `TDUR_CURSOR_DIAGNOSTICS=1`) are a separate feature, described
in [cursor and activation diagnostics](cursor_activation_diagnostics.md).
The watchdog reads window state with its own calls and does not use them.

## Channel between the launcher and the DLL

The launcher sets `TDUR_CRASH_CHANNEL=1` in the environment of the child. In
the DLL, an absent variable leaves the handler disabled (record
`crash-handler-disabled`, reason `no-channel`) and initialization goes on;
exactly `1` arms it; any other value, `0` included, makes initialization
fail (`crash-handler-install-failed`, reason `invalid-variable`).

After the suspended child has joined its job and its creation time has been
read, and before the child is resumed, the launcher creates four objects
named:

```text
Local\TDUR-CRASH-<pid>-<creation>-<suffix>
```

`<pid>` is the process ID of the child and `<creation>` its creation time as
a 64-bit `FILETIME` value, both in decimal. The suffixes are:

- `channel`: a 4096-byte section backed by the paging file, of which the
  first 128 bytes are used;
- `request`: an auto-reset event that the DLL sets when it claims a crash;
- `done`: a manual-reset event that the launcher sets when the crash dump is
  finished or abandoned;
- `image`: an auto-reset event that the DLL sets for calls made from the
  main image.

Creation fails if any of these objects already exists; the launcher then
ends the suspended child and exits with an error. The DLL rebuilds the same
names from its own process ID and creation time, opens the section and the
events, and checks the header: magic, size 128, layout 1, process ID and
creation time.

Installation failures are written to the observation log as
`crash-handler-install-failed` with one reason: `process-times`, `name`,
`channel-missing`, `channel-map`, `channel-header`, `channel-events`,
`target-missing` or `detour`. Once the target is resolved, the DLL writes
`crash-handler-target` (`same=1` when the `kernel32.dll` export resolves to
the same address as the `kernelbase.dll` one, `same=0` otherwise). Once the
detour is in place, it binds the heartbeat to the channel, sets `mod_state`
to 1 and writes `crash-handler-armed`.

| Offset | Field | Writer |
| --- | --- | --- |
| 0 | `magic`: the eight bytes of `channel_magic` | launcher |
| 8, 12 | `size` (128), `layout` (1) | launcher |
| 16 | `pid` | launcher |
| 24 | `creation_filetime` | launcher |
| 32 | `mod_state` (1 once armed) | DLL |
| 36 | `crash_state` | DLL, launcher |
| 40 | `present_beats` | DLL |
| 44, 48 | `crash_entries`, `image_calls` | DLL |
| 52 to 92 | thread, exception data, registers, caller | claiming thread |
| 96, 112 | `real_filter_result`, `real_filter_done` | claiming thread |
| 100 | `image_last_code` | DLL |
| 104 | `claimed_tick` | claiming thread |
| 116 | `exception_params` | claiming thread |
| 120 | `last_entry_code` | DLL |

The launcher initializes `real_filter_result` to `0xFFFFFFFF`. The
`crash_state` values are 0 idle, 1 claimed, 2 dumped, 3 dump failed, 4 DLL
wait timed out and 5 request invalid. The DLL moves the state from idle to
claimed and from claimed to timed out; the launcher writes the other values.

## Crash path

The detour handles each call to `UnhandledExceptionFilter`:

- A call whose return address lies in the main image, such as
  `__except (UnhandledExceptionFilter(...))`, increments `image_calls`,
  stores its code in `image_last_code`, sets `image` and goes straight to
  the real filter. It is never dumped.
- Any other call is a genuine entry: `crash_entries` grows and
  `last_entry_code` takes its exception code. The first genuine entry claims
  the channel with an atomic compare-exchange and records the thread ID, the
  `EXCEPTION_POINTERS` address, the return address of the detour, the
  exception code, flags, address, parameter count and first two parameters,
  EIP, ESP, EBP and a `GetTickCount64` value. It then sets `request` and
  waits for `done` for at most 50 seconds; on timeout it marks the wait as
  timed out.
- A later genuine entry made while the claim is pending waits for `done`
  with the same limit. Later entries never overwrite the fields of the
  claiming thread.
- The real filter is then called and its result is returned unchanged. The
  claiming thread copies that result to `real_filter_result` and sets
  `real_filter_done`.

The detour allocates nothing, writes no log, takes no lock and loads no
library, and its stack frame is kept small so that a stack overflow can
still reach the real filter. The 50-second limit (`mod_wait_ms`) is checked
at compile time against twice the 20-second helper limit plus 5 seconds: a
crash raised while a hang dump is running can wait for that dump and then
for its own.

The launcher waits on the child process and on the `request` and `image`
events in turns of 100 ms. When `request` is set, it:

1. checks that the state is claimed; otherwise it records
   `crash-request-unclaimed` and goes to step 6;
2. checks that the recorded thread belongs to the child; otherwise it sets
   the state to request invalid, records `crash-thread-foreign` and goes to
   step 6;
3. reads the range of the main image of the child, so that report addresses
   inside that image are written as RVAs;
4. runs the dump helper with `--kind crash`, the thread ID and the
   `EXCEPTION_POINTERS` address, which writes `tdur-crash-1.dmp`;
5. sets the state to dumped if the helper exited with 0, to dump failed
   otherwise;
6. sets `done` in every case, including after an internal failure, so that
   the faulting thread always goes on to the real filter.

The `image` event only wakes the launcher; main-image calls appear as
counters in the report. If the wait on these objects fails, the launcher
records `wait-multiple` and from then on waits on the process alone; a later
crash request is then released only by the 50-second limit of the DLL.

Before starting the child, the launcher adds `SEM_FAILCRITICALERRORS`,
`SEM_NOGPFAULTERRORBOX` and `SEM_NOOPENFILEERRORBOX` to its own error mode,
which the child inherits; the PID file records that mode as
`inherited_error_mode`. The job of the child uses
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`,
`JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION` and an active process limit of
one, so the child cannot outlive the launcher. Once the child runs, the
crash and hang steps never end the launch through an exception, since
closing the job would end the child: each step records its failure in
`diagnostic_errors` instead.

## Hang detection

The launcher samples the child at most once per second, from its wait loop.
Time comes from `QueryUnbiasedInterruptTime`. Each step between two samples
counts at most 2 seconds, and the first sample or a clock going back counts
zero.

The launcher follows one window: the visible top-level window of the child
that has no owner and the largest area. It keeps that window while it still
belongs to the child and is visible or hung, and searches again otherwise.
For that window it reads `IsHungAppWindow`, `IsIconic` and whether the
foreground window belongs to the child. It reads no window text or class and
sends no message to the child; the report says so with
`window_text_collected` and `window_messages_sent` set to `false`.

The rules of `watchdog_core.cpp` are:

- While a crash is claimed, no time accumulates and no action is taken.
- A hang episode starts at the first sample whose window is known and hung
  according to `IsHungAppWindow`, and grows while the next samples stay
  hung. One responsive sample, or a sample without a window, ends it.
  Episodes longer than one second are kept, up to 64; further ones are only
  counted.
- When the episode reaches the dump threshold (`--hang-dump-after`), the
  launcher writes a first dump, once per episode.
- With the `ask` policy, when the episode reaches the question threshold
  (`--hang-ask-after`), the launcher writes a second dump and then asks the
  question, once per episode. A yes answer stops the child at the next
  sample that is still hung in the same episode.
- With the `stop` policy, when the episode reaches the stop threshold
  (`--hang-stop-after`), the launcher writes a second dump and then stops
  the child.
- With the `dump-only` policy, the first dump is the only action.
- A minimized window (`IsIconic`) delays the question and the automatic
  stop, together with the second dump that comes with them; the first dump
  is not delayed. The hung time spent waiting for the window is reported as
  `deferred_iconic_ms`.
- Heartbeat rule: once `present_beats` is above zero, a stall time grows
  while the counter stays unchanged and the window is known, in the
  foreground, not minimized and not hung; any other sample resets it. When
  the stall reaches `--beat-dump-after`, the launcher writes a heartbeat
  dump, at most once per session. This rule never asks and never stops.
- A session writes at most four hang and heartbeat dumps in total.

The heartbeat counter is incremented at the start of the
`IDirect3DDevice9::Present` and `IDirect3DSwapChain9::Present` hooks of the
display bridge, except for the internal calls of the bridge. These hooks
exist only when the D3D12 display bridge is enabled (`TDUR_D3D12_DISPLAY=1`,
see [the bridge note](display_hooks.md)). The launcher does not set that
variable; the child inherits it from the environment of the launcher.
Without it the counter stays at zero and the heartbeat rule never applies.

To stop a hung child, the launcher records a `stop` object, prints
`game-stopped reason=hang hung_ms=<ms>` and ends the job of the child with
exit code `0xE1480001`. If the job cannot be ended, it records `hang-stop`
and drops the `stop` object; the child keeps running.

## Launcher options

These options are optional and come in addition to the ten required
launcher options. Each may appear once; thresholds are whole numbers of
seconds.

| Option | Accepted values | Default |
| --- | --- | --- |
| `--hang-action` | `dump-only`, `ask`, `stop` | `dump-only` |
| `--notify` | `console`, `dialog` | `console` |
| `--hang-dump-after` | 6 to 600 | 15 |
| `--hang-ask-after` | 60 to 1800 (7 to 1800 in test mode) | 60 |
| `--hang-stop-after` | 120 to 3600 (7 to 3600 in test mode) | 120 |
| `--beat-dump-after` | 120 to 3600 (7 to 3600 in test mode) | 120 |
| `--hang-test-thresholds` | `1` only | absent |

The launcher also enforces these rules before it creates any process. A
refusal ends it with exit code 2 and a message on standard error.

- The question and stop thresholds must both exceed the dump threshold,
  whatever the policy.
- `--hang-action ask` requires `--notify dialog`.
- `--hang-action stop` is refused with `--session-mode play`: the automatic
  stop is meant for sessions without a player.
- `--hang-test-thresholds 1` selects test mode, which lowers the floors of
  the question, stop and heartbeat thresholds to 7 seconds. The report
  records it as `test_thresholds` in its `watchdog` section.

With `--notify console`, hang events appear only as lines on standard
output; `--notify dialog` allows the question box, which only the `ask`
policy opens. In `--session-mode play` the `--max-seconds` deadline does
not apply; in the default `diagnostic` mode it ends the child after the
given number of seconds, from 1 to 120.

## Question to the player

The question opens only with the `ask` policy and `--notify dialog`, when an
episode reaches the question threshold while the window is not minimized,
and never twice at the same time. It is a standard message box shown by a
separate launcher thread, so sampling goes on while it is open.

- Title: `TDU Recompilation: game not responding (PID <pid>)`.
- Text: "The game has not responded for N seconds. A dump was written for
  diagnosis.", then "Close the game now?", then one line per answer: Yes
  closes the game, and the isolated copy of the save may remain incomplete
  while the original saves are not touched; No keeps waiting.
- Buttons: Yes and No, with a warning icon. No is the default button. The
  box stays above other windows (`MB_TOPMOST`) and is opened without
  `MB_SETFOREGROUND`, so it does not ask for the foreground.

The answers lead to:

- Yes: the answer is stored on its episode and the child is stopped at the
  next sample that is still hung in that episode, with exit code
  `0xE1480001`. If the window responds first, nothing is stopped.
- No: the answer is stored and the launcher keeps waiting. That episode gets
  no second question, and the `ask` policy never stops the child by itself.
- No answer: nothing happens. When the episode ends or the child exits, the
  launcher closes the box itself and stores `auto-closed`; if the box is
  still open 2 seconds later, it also records `question-not-closed`.
- An answer read after its episode has ended is stored on that episode with
  `answer_late`, counted in `answers_after_episode` and recorded as
  `answer-after-episode`. It never stops another episode.

Standard output shows `hang-question-open episode_ms=<start>` and
`hang-question-answer answer=yes|no|auto-closed`.

## Dump helper

The launcher looks for `tdur_dump_helper.exe` next to its own executable and
refuses to start when it is missing. Before it locks the source saves or
creates the child, it runs the helper with `--self-check` in a confined job,
with standard output read through a pipe and a 10-second limit. The helper
must exit with 0 and print one JSON object (`self_check`, the `dbghelp`
path, its `version`, `symopt_ignore_cvrec` and `symbols_loaded`); otherwise
nothing is launched. That output and the SHA-256 of the helper go to the
`helper` section of the report.

For each dump, the launcher:

- creates `<stem>.dmp` and `<stem>.json` in the session folder, never over
  an existing file;
- duplicates its handle to the child with `PROCESS_QUERY_INFORMATION`,
  `PROCESS_VM_READ` and `SYNCHRONIZE` rights only;
- starts the helper suspended, without a window, in a job of its own (kill
  on close, one active process), with the session folder as current
  directory and only those three handles inheritable
  (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`);
- ends the job of the helper if the helper still runs after 20 seconds, and
  records `helper-timeout`;
- reads the exit code, the dump size, the SHA-256 of a non-empty dump and
  the summary, which is kept only if it has the shape of one JSON object.

The command line of the helper is:

```text
tdur_dump_helper.exe --kind crash|hang --process <handle>
    --dump <handle> --summary <handle> --pid <pid>
    --creation-filetime <filetime>
    [--thread <tid> --exception-pointers <address>]
```

The last two options are required for `crash` and refused for `hang`.
Numbers are decimal or `0x` hexadecimal and must not be zero. Before
writing, the helper checks that the process handle matches the PID and the
creation time, that the process is still running and that both files are
empty disk files; otherwise it exits with 11 and writes nothing.

The helper restricts DLL loading to the system directory
(`LOAD_LIBRARY_SEARCH_SYSTEM32`), loads `dbghelp.dll` and `version.dll` from
there, never initializes symbols, and sets `SYMOPT_IGNORE_CVREC` and
`SYMOPT_DEFERRED_LOADS`. The minidump type combines `MiniDumpNormal`,
`MiniDumpWithThreadInfo`, `MiniDumpWithUnloadedModules`,
`MiniDumpWithFullMemoryInfo`, `MiniDumpWithProcessThreadData` and
`MiniDumpIgnoreInaccessibleMemory`; full memory, data segments, indirectly
referenced memory and handle data are not requested.

Through the minidump memory callback the helper adds a few ranges. Each one
is clipped to the committed readable region that holds its anchor and is
never taken inside the game image, which is the first module of the target:

- for a crash, 2,048 bytes on each side of the faulting address and 128
  bytes on each side of the accessed address, skipped when they fall in an
  image region;
- the 4-byte `ntdll!Wow64Transition` cell, 32 bytes at its target and the
  first 16 bytes of `ntdll!NtWaitForMultipleObjects`, when the `ntdll.dll`
  base of the helper appears among the modules of the target;
- the 24-byte `FastPebLock` and `LoaderLock` critical sections, found
  through the pointers at PEB32 + 0x1C and PEB32 + 0xA0, whose owning
  threads are reported.

For a crash, the dump carries an exception stream built from the thread ID
and the `EXCEPTION_POINTERS` address inside the target. If the write fails
with that stream, the helper empties the file and writes again without it.

The summary file holds `kind`, `pid`, `thread_id`, `flags`, `written`,
`exception_stream`, `first_error`, `error`, `bytes`, `ms`, `dbghelp`,
`dbghelp_version`, `symopt_ignore_cvrec`, `symbols_loaded`, `exception`
(crash only), `wow64_transition`, `loader_lock_owner_tid`,
`fast_peb_lock_owner_tid`, `game_image`, `added_ranges` and `notes`. The
exit codes of the helper are 0 (dump written), 10 (invalid arguments), 11
(identity mismatch), 12 (dbghelp unavailable), 13 (dump failed) and 14
(summary not written).

## Files and report

The session folder is the folder that contains the report file
(`--report`). The launcher refuses to start unless that folder is the
`--private-documents` folder or its parent, and lies outside the
`--source-documents` folder. It holds:

- `tdur-crash-1.dmp` and `tdur-crash-1.json`, the crash dump and its helper
  summary, at most once per session;
- `tdur-hang-<n>.dmp` and `tdur-hang-<n>.json`, with `n` from 1 to 4, for
  hang and heartbeat dumps in the order they were written;
- the report itself.

The observation log (`--log`) must lie inside the `--private-documents`
folder, which is the session folder or one of its direct subfolders; the
installation records of the DLL go there. The report is written once the
child and every process of its job have stopped. This subsystem adds the
following sections to it:

- `helper`: path, SHA-256 and self-check output of the helper;
- `crash_channel`: `created`, `mod_armed`, `present_beats`,
  `crash_entries`, `image_calls`, `image_last_code` and `last_entry_code`;
- `crash`: `null`, or the claimed crash with `thread_id`,
  `exception_code`, `exception_flags`, `exception_address`, `access`
  (`kind`, `target`), `parameters`, `real_filter_result`, `mod_wait`
  (`done` or `timeout`), `later_entries`, `process_alive_after_dump_ms`
  and `dump`. `real_filter_result` is `execute_handler`, `continue_search`,
  `continue_execution`, `other` or `unknown`. Addresses inside the main
  image are written as `{"main_rva": ...}`;
- `watchdog`: the settings (`poll_ms`, `policy`, `notify`,
  `test_thresholds`, `thresholds_ms`), `window_seen`, `samples`,
  `longest_hung_ms`, `beat_longest_stall_ms`, `deferred_iconic_ms`,
  `answers_after_episode`, `episode_open_at_exit`, `episodes`,
  `episodes_dropped` and `beat_dump`, the sequence number of the heartbeat
  dump or 0. Each episode has `start_ms`, `hung_ms`, `dumps` (sequence
  numbers), `asked`, `answer`, `answer_late` and `stopped`;
- `hang_dumps`: one record per hang or heartbeat dump, with `file`,
  `helper_exit`, `started`, `timed_out`, `error`, `error_step`,
  `helper_ms`, `bytes`, `sha256` and `summary`; `crash.dump` uses the same
  record;
- `stop`: `null`, or `by`, `reason`, `policy`, `hung_ms`, `answer` and
  `exit_code_requested`;
- `diagnostic_errors`: up to 32 records of a `step` and a Win32 `error`;
- `end_reason`, described below.

`end_reason` takes the first value whose condition holds:

| Value | Condition |
| --- | --- |
| `deadline` | the `--max-seconds` deadline ended the child |
| `hang-stopped` | the launcher stopped the child for a hang |
| `crash-recovered` | claimed crash, real filter result `continue_execution` |
| `crash-dumped` | claimed crash, helper exit code 0 |
| `crash-not-dumped` | claimed crash, no successful dump |
| `closed-during-hang` | episode open at exit, or exit code `0xCFFFFFFF` |
| `startup-failed` | exit code `0xC0000142`, channel never armed |
| `abnormal-exit-no-signal` | exit code `0xC0000000` or above, no claim |
| `exit` | any other end |

The launcher prints these lines on standard output:

- `hang-dump file=tdur-hang-<n>.dmp exit=<code> cause=window|beat`, where
  `exit` is the exit code of the helper, or -1 when it did not start;
- `hang-question-open` and `hang-question-answer`, described above;
- `game-stopped reason=hang hung_ms=<ms>`;
- `game-ended reason=<end_reason> exit=<code>`.

The `step` values of `diagnostic_errors` are `crash-request`,
`crash-request-unclaimed`, `crash-thread-foreign`, `wait-multiple`,
`watchdog`, `hang-stop`, `question-thread`, `question-close`,
`question-not-closed` and `answer-after-episode`, and for the helper
`helper-dump-file`, `helper-summary-file`, `helper-process-handle`,
`helper-attributes`, `helper-handle-list`, `helper-job`, `helper-start`,
`helper-confine`, `helper-timeout`, `helper-summary-invalid` and
`helper-exception`.

## Reading a dump without dbghelp

```powershell
py -3 -I tools/minidump_summary.py tdur-hang-1.dmp
py -3 -I tools/minidump_summary.py tdur-crash-1.dmp --range 0x00401000
```

`tools/minidump_summary.py` reads one x86 minidump and prints a JSON summary
on standard output; it writes no file. It checks the `MDMP` signature and
every offset against the file size. A truncated or inconsistent file gives
`minidump error: ...` on standard error and exit code 2, never a partial
result. It decodes the system information (architecture), the thread list
(IDs, suspend counts, TEB, stack ranges, and EIP and ESP from each x86
context), the module and unloaded module lists, the memory list, the
exception stream, the memory region map (state, type, protection) and the
thread information list. Other streams are listed by type and size only.

- `--range ADDRESS` adds the region of the map that contains the address,
  or `null`.
- `--bytes ADDRESS LENGTH` adds the captured bytes in hexadecimal, or `null`
  when they are not inside one captured range. It prints raw memory from
  the dump and is meant for tests.

Addresses and lengths are Python integer literals, such as `0x00401000`.
The test fixtures also import its `parse`, `region_for` and `memory_bytes`
functions.

## Tests

Every test below is registered by `mod/launcher_tests/crash_tests.cmake` in
the Win32 mod project; the
[build guide](../building.md#configure-the-win32-mod-separately) shows how
to build and run mod tests. No test starts the commercial game. The
launcher scenarios run `tdur_crash_witness`, a program that crashes or
hangs on purpose according to `TDUR_CRASH_WITNESS_MODE`, under the real
launcher and the real observation DLL. The witness writes its process ID,
thread IDs and addresses before acting, so that the fixtures can compare
them with the dumps.

Components:

- `crash_channel_layout`: the layout, whose size and offsets are also
  asserted at compile time, the exact object names, a buffer one character
  short refused with nothing written past it, the header checks (magic,
  size, layout, PID, creation time), heartbeat counting, the hang stop exit
  code, and the DLL wait covering two helper runs plus 5 seconds.
- `crash_handler_chain`: the real `crash_handler.cpp` armed in a test
  process, with a simulated launcher thread. A first genuine entry is
  claimed and signalled, the launcher answers before the real filter runs,
  the filter result comes back unchanged and every field is recorded. A
  second entry overwrites nothing and sends no request. A call from the
  main image is counted and signals `image` but is never claimed.
- `crash_handler_absent`, `crash_handler_invalid`, `crash_handler_missing`
  and `crash_handler_foreign`: no variable (disabled, success), value `2`
  (refused), no channel (refused) and a header written for another PID
  (refused). Each time the first bytes of `UnhandledExceptionFilter` stay
  unchanged and the handler is never armed.
- `watchdog_decisions`: simulated timelines. No dump at 14.9 seconds and one
  at 15; with `ask`, dumps at 15 and 60 seconds and the question at 60; a
  yes stops at the next sample; a no never stops over ten more minutes; with
  `stop`, dumps at 15 and 120 seconds and the stop at 120; a single dump in
  `dump-only`; a reset by one responsive sample; a three-hour clock jump
  counted as 2 seconds; nothing during a crash; a heartbeat dump at 120
  seconds in the foreground and none in the background; the caps of four
  dumps and 64 episodes; the delay while minimized; late answers.

Dump helper alone, on witnesses started by the fixture:

- `dump_helper_self_check`: exit code 0, dbghelp from System32 or SysWOW64,
  a version, `SYMOPT_IGNORE_CVREC` set and no symbol loaded.
- `dump_helper_refuses`: invented handle values (exit code 10 or 11, no file
  created); unknown, repeated or inconsistent arguments (exit code 10); a
  wrong creation time (exit code 11, files left empty) or a wrong PID (exit
  code 11). The import
  table holds no `dbghelp.dll`, no `advapi32` and none of `OpenProcess`,
  `CreateFileW`, `CreateFileA`, `CreateProcessW`, `CreateProcessA`,
  `OpenThread`, `WriteProcessMemory` or `DuplicateHandle`, and the file
  contains no `SymInitialize`, `SymLoadModule` or `RegSetValue` string.
- `dump_helper_dump_readable`: a hang dump of an idle witness, which stays
  alive. The summary size matches the file; the reader finds an x86 dump,
  the main thread among at least two threads, the witness and `ntdll.dll`
  modules and a region map; the added ranges include the
  `NtWaitForMultipleObjects` stub and the loader lock. When the PDB of the
  witness exists, it can then be opened exclusively.
- `dump_helper_loader_lock`: the same with a witness thread that holds the
  loader lock; the summary names that thread as the owner.
- `dump_helper_suspension`: `tdur_dump_helper_stall`, a helper variant that
  stays 30 seconds inside the dump. The 50 ms heartbeat thread of the
  witness stops during the dump and runs again once the helper is killed.
  The build file names dbghelp 6.2.26100.9818 for this measurement.
- `minidump_summary_parser`: the reader on a synthetic dump. Exact fields,
  captured ranges and bytes, every truncated prefix refused and a wrong
  signature refused.

Through the launcher:

- `crash_launcher_crash_exec_private`: a worker thread executes a private
  read-write page (code `0xC0000005`, access kind 8). One
  `tdur-crash-1.dmp`, whose size and SHA-256 match the report, is written
  in under 5 seconds. Its exception stream names the faulting thread; it
  holds at least two threads, the witness, `dinput8.dll` and `ntdll.dll`,
  the 16 sentinel bytes at the faulting address and a private region there;
  the `Wow64Transition` target was read. The top-level filter of the witness
  runs after the dump exists, the end reason is `crash-dumped`, and the exit
  code equals that of a run without launcher and DLL.
- `crash_launcher_crash_write_reserved`: the main thread writes to a
  reserved page (access kind 1). The checks of the previous scenario,
  except the sentinel bytes and the private region; instead, a `reserve`
  region at the target and no new Windows Error Reporting entry named after
  the witness.
- `crash_launcher_crash_stack_overflow`: a stack overflow on a worker
  thread. One dump with code `0xC00000FD`, a successful helper and
  `crash-dumped`. An exit code that differs from a run without the launcher
  is accepted only with a recorded later entry.
- `crash_launcher_crash_handled`: two runs, a fault handled by the `__except`
  of the witness (mode `crash-handled`) and a deliberate
  `UnhandledExceptionFilter` call from its main image (mode
  `uef-image-call`). No dump, `crash` is `null`, the end reason is `exit`,
  and one image call is counted in the second run only.
- `crash_launcher_hang_ui_stop`: the window thread sleeps forever; policy
  `stop` with test thresholds of 6, 7 and 9 seconds. Two dumps that both
  contain the main thread, a stop after at least 9 seconds of hang, exit
  code `0xE1480001` and `hang-stopped`.
- `crash_launcher_hang_ui_ask_yes` and `crash_launcher_hang_ui_ask_no`:
  launcher variants whose question returns a fixed answer instead of opening
  a box, in a `play` session with policy `ask`. One question, after the
  second dump; yes stops the child (`0xE1480001`, `hang-stopped`); no keeps
  it until it recovers and exits by itself (`exit`).
- `crash_launcher_hang_cross_thread`: the main thread blocks on a critical
  section held by another thread. One dump and no stop before the deadline.
- `crash_launcher_hang_loader`: the main thread blocks on the loader lock,
  held by a thread inside the `DllMain` of `tdur_loader_hold.dll`. One dump
  whose summary names the holder.
- `crash_launcher_busy_pumping` and `crash_launcher_stutter`: respectively,
  busy work that keeps pumping messages, and four pauses of 4 seconds. No
  dump, the end reason is `exit`, and every episode stays under 15 seconds.
- `crash_launcher_foreign`: a hung copy of the witness runs outside the job
  of the launcher while the child stays idle. The copy keeps running, no
  dump is written and the report names no foreign PID.
- `crash_launcher_helper_gone`: the helper is removed after the self-check.
  The hang dump fails at `helper-start`, the error is reported and printed
  (`exit=-1`), and the child runs until the deadline.
- `crash_launcher_options`: seven invalid option sets and a report outside
  the session folder are refused with exit code 2 before any process is
  created.

Every launcher scenario except `crash_launcher_options` also checks that the
source saves are unchanged,
that no window text was collected and no message sent, that the channel was
created and armed, that the observation log contains `crash-handler-armed`
and that standard output carries the `game-ended` line. The existing
`observation_launcher_lifecycle` test also requires an armed channel and an
end reason among `exit`, `deadline` and `abnormal-exit-no-signal` for its
five fixture modes.

## Limits

- The tests run only source-owned programs. They do not establish how these
  diagnostics behave in the commercial game: whether its failures reach
  `UnhandledExceptionFilter`, whether the window chosen is its main window,
  or whether its hangs show through `IsHungAppWindow`. No result here is a
  game validation.
- No test opens the real question box: the launcher variants return a fixed
  answer. Its wording, buttons and focus behaviour are known from the code
  only. The text always says that a dump was written, even when the dump
  cap was reached or the helper failed.
- The delay for a minimized window, the heartbeat rule, the 2-second step
  bound and the episode cap are verified on simulated timelines only.
- The heartbeat depends on the display bridge; without it the counter stays
  at zero.
- Only the state of the chosen window, the heartbeat counter and the crash
  state enter the hang rules. A child that keeps processing window messages
  is never in a hang episode, whatever its other threads do; the watchdog
  measures no CPU, thread or GPU activity.
- A process gets at most one crash dump; a session gets at most four hang or
  heartbeat dumps and keeps at most 64 episodes.
- A termination that does not go through `UnhandledExceptionFilter`, such
  as an explicit `ExitProcess` call, gives no crash dump; with an exit code
  of `0xC0000000` or above, the report then says `abnormal-exit-no-signal`.
- After a stack overflow the dump is written, but the chained filter can
  fault again for lack of stack, so the exit code may differ from a run
  without the launcher.
- While the helper runs, for up to 20 seconds, the launcher neither samples
  the window nor serves a crash request. The hung time across that gap
  counts at most 2 seconds, and a crash raised meanwhile waits in the DLL.
- Ending a helper at its limit assumes that the child resumes when the
  helper ends, which `dump_helper_suspension` measures with the installed
  dbghelp.
- Only x86 targets are handled: the channel stores 32-bit addresses, the
  helper reads 32-bit PEB and critical section layouts, and the reader
  decodes x86 thread contexts.
- No symbol is loaded and no function name is resolved.
- These diagnostics do not explain or fix any crash or hang of the game.
