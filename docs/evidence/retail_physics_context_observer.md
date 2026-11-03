# Observation of the physics callback floating-point context

## Scope

The diagnostic option `--physics-context`, disabled by default, observes at
most 32 **entries** of `81EE50` on the isolated official copy. It provides no
schedule, does not modify the flags or the floating-point context and does not
replace the integrator. A reentrant entry, one too close or one skipped by ED
remains an observed entry: it is never counted as a physics step.

Reference: PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The need comes from the
[SIMD division by a zero delta and the thread dependencies](retail_physics_thread_dependencies.md).
This observation does not prove TLS/Havok independence or the state of a
loaded world. Official trial 20 now contributes 32 instrumented entries,
presented below with their attribution and timing limits.

## Admission and transfer

The following extension also preserves the gate's return address. It is
taken by the `_ReturnAddress()` intrinsic at the gate level, before its single
call to `Capture`; it is not taken inside the latter.
The immutable payload publishes it with the same guarantees as the FPU words.
Outside the callback, the logger classifies the module containing this address
through the loader metadata. It neither reads nor executes the code at this
address. This classification describes the mapping at **logging
time**: unloading or reuse of a range can change the attribution. It therefore
does not demonstrate a particular WinMM function or a capture thread identity.
A separate comparison with the already loaded export of `winmmbase` completes
the cell check, still without following the cell.

The extended fixture passes **111 checks**, including a comparison of the
captured address with the one seen by the synthetic original body itself. The
new production object keeps a `Capture` without any CALL; the gate loads its
return from `[EBP+4]`, passes two arguments to `Capture`, restores the stack,
then tail-JMPs to the original with its five native arguments. The 22 CTests of
the observation mod pass with this extension and the launch FILETIME. No new
official trial has yet been assigned to this extension.

The launcher verifies the file's SHA, keeps it locked and overwrites the
inherited marker. The module refuses late loading. The pre-entry transaction
checks name, base, x86 PE and marker, then the complete five-byte prefix
`MOV AL,[10E7C08]` and the terminal `RET 14h` at `81F0A1`.
The trampoline relocates this MOV and resumes at `81EE55`. After commit, the
two jumps, the original/RX rights and the flushes are checked; a failure
refuses startup. No protection mechanism is altered.

The wrapper has exactly the ABI
`void CALLBACK(UINT,UINT,DWORD_PTR,DWORD_PTR,DWORD_PTR)`.
The five DWORDs are passed intact, once only, to the original routine.
Its guard `10E7C08`, its flags and all its decisions remain original.
The hook and its data stay loaded until the end of the process; no arbitrary
detachment or rearming of the slots is planned in production.

## Capture and publication

The hot path starts with **FNSTCW**, with no FWAIT, then STMXCSR. It
uses no floating-point arithmetic, no write to any setting, no API, CRT,
allocation, blocking lock or guarded TLS/static initialization. Lock-free 32-bit
atomics reserve one of the 32 slots through a saturating CAS. Each payload
becomes immutable before its ready flag is published with release semantics; the
reader consults this flag with acquire semantics. No wrapping counter can reuse
a filled slot.

The preserved fields are the x87 control word, the raw MXCSR, the received
timer identifier and, since the extension described above, the return address.
No userdata pointer, key, account, pixel, clock or game thread
is read on this path. The reserved counter is read after the slots:
a reading may contain reservations not yet published, but its ready count
cannot exceed the reservations. It remains an observation over
an interval, with no atomic snapshot common to the 32 slots.

Logging happens after an `S_OK` DXGI present, on the ordinary thread that
owns the window. Each slot is written once; bounded summaries are emitted at
presents 1, 1024 and 4096. Without a successful present, the samples remain
stored but are not necessarily logged.
MXCSR is also split into control and persistent status flags: a status
difference does not prove a difference in masks or rounding.

Outside the loader lock and outside the callback, a four-byte read of the cell
`9EC4A8`, if its main image page is readable and non-executable, is compared
with `GetProcAddress(winmm,timeSetEvent)`. Only availability and equality
booleans are logged. The cell is neither followed nor called.
The worker `81F0B0/223` must also be corroborated by the independent probe
before this path is attributed to WinMM; this comparison does not prove the
API call for each sample. The message therefore keeps the term "entry".

## Validation before any official trial

Windows x86 fixture owned by the project, C++latest MSVC 19.44, `/W4 /WX`:
**78 checks**. It uses a real Detours on a stub carrying the same five-byte
MOV, then verifies the stack, the five arguments, the single transfer, the
floating-point controls/statuses, four concurrent producers, one concurrent
reader, saturation and immutability. The logger stays outside the callback,
duplicates are removed and the stub's code is restored after detachment.
This fixture detachment at a quiescent point is not a mechanism shipped
for the game. A cell absent from the fixture refuses any native attribution.

The **production** object is disassembled with dumpbin: `Capture` contains
FNSTCW, STMXCSR, integer operations and LOCK CMPXCHG, without any CALL. The gate
calls only `Capture`, then performs a tail-JMP to the original without changing
the argument stack. The assemblies and fingerprints are kept in
`<private-evidence>/reprise-2026-10-30/physics-entry-context-cpp/` outside the
repository. An independent review checked these elements. Its remark about the
counter being read too early was corrected and covered by the concurrent reader.

The **22 CTests of the observation mod** pass before this summary correction and
the addition of the cell check; the three targeted capture/startup/exclusive
tests then pass with the current DLL. Visualizations use synthetic pixels,
without any game capture.

The separate `native-adapter` profile shipped for trial 20 adds 16 named
executable reads, or 2 513 bytes, to the 51 ranges of the display profile:
**67 / 14 863**. The AddressSets `43BD90/409` and `43BF30/144` exclude their
seven-byte hole. The priority table lies in `.text` but is still described as
data. The corresponding scalar profile adds the one-byte endian flag and the
four-byte cell: **30 fields / 96 bytes** per series, without following any
pointer. Earlier profiles keep their budgets and the two read-only self-tests
pass.

## Official trial 20: bounded result

Closed trial `20261030T134824-33340`, PID **29576**, isolated official copy
verified and locked. Requested duration 75 s; the launch report indicates
75.1912 s and termination by the Job object at the deadline, code 1460. The slot
is released; the 6 697 reference files and the 17 before/after save fingerprints
match. This is not a trial of the secondary binary.

Observer DLL of this trial, SHA-256:
`3845d29e1a5ce5ffe8f49e57a5f9e295ea96bcb3c0d25d2ab7cff0985cfc2064`.
The `physics-context-gate-armed-pre-entry` events, then the 32
`physics-context-entry` lines, are present in the log. Slots 0 to 31 were
reread and compared with the `physics-context.json` summary:

| Captured field | Result over the 32 published slots |
| --- | --- |
| x87 control word | `027F`, uniform |
| MXCSR control | `00001F80`, uniform |
| Raw MXCSR | `00001F80` once; `00001FA4` 31 times |
| MXCSR status | `00` once; `24` 31 times |
| Received `timer_id` parameter | 16, uniform |

The status difference does not constitute a difference in the controls.
The origin of the persistent flags `24` is not established by this capture.
These values describe these entries of `81EE50`, without fixing a universal
preset for all threads, the CRT or Havok.

The lines are published later by the ordinary display thread. Their `tick` and
`tid` are those of the **logger**, not of the captures. The hot path samples
neither an individual timestamp nor a thread identifier. The saturating capacity
of 32 slots does not count all later entries; neither a frequency, nor a
duration between callbacks, nor a number of physics steps can be deduced from
it. None of the 32 slots constitutes proof of a completed integration.

### Timer attribution and scalars

At display initialization, the observer logs for the cell `9EC4A8`:
`readable=1`, `api_available=1`, **`matches_timeSetEvent=0`**. The API is
available but equality is not established at that instant. The body of the
worker `81F0B0/223` remains corroborated within the bounds of the independent
probe; this does not turn this negative test into an attribution to WinMM.

The cell appears as the `timeSetEvent` import in the secondary; the official PE
inventory places its import at `01F3E1A8`, which is not read by this catalog.
The result establishes neither the cause of the discrepancy nor the destination
of the pointer. **No cell value is followed or called.** The received parameter
16 therefore does not prove, by itself, an emission by the verified WinMM API.

The `native-adapter` scalar probe records **26 samples of 30 fields,
96 bytes each**, over about 5.28 s, without any read error. Over this
series: `F56A70=1`, `129BEED=1` and integrator entry counter `16A69F0=0`.
The cell is also readable as a 32-bit integer, without dereferencing.
This series and the FPU captures do not constitute a common snapshot:
the 32 entries cannot be attributed to the ED branch on this basis alone,
nor can the zero counter be extended to the whole duration of the trial.

### Code present, own modifications and end of trial

The code profile records **67 ranges / 14 863 bytes**. Its 16 additions are all
identical to the secondary and different from the official disk image, with no
own modification in these additions. The eight physics boundaries, their
AddressSets, the table and the five EdNet bodies are detailed in the runtime
code helpers note.

Five other ranges carry the known adaptations of the project's module:
`81EE50/5`, `996850/7`, `97F2C8/2`, `97F668/2`, `7BEC34/2`, or **18 bytes**.
These locations are explicitly distinguished in the comparisons;
they are never presented as intact original bytes. The agreement
outside own modifications is not a global equality of the image.

The summary then records a scalar attempt **after the exit of the owned
process**: refusal `Cannot open target read-only`, without creation of
`timing-late.json`, whose absence is verified. This late refusal is neither an
additional measurement nor a game failure; no relaunch follows from it.

These results establish FPU controls observed at entry and code
present. They still do not validate driving, a loaded world, the TLS/Havok
transfer, fixed physics at 90 Hz, NPCs at 500 FPS or an EdNet session.
No new build, game or process read was executed for the
present documentary verification.

## Next integration: return address and creation identity

The reinforced fixture passes **111 checks**, including the equality of the
captured address with the return observed by the owned original body. The
production gate loads the native return from `[EBP+4]`, calls only Capture,
restores the stack and jumps to the original body. Capture remains without any
CALL. Module classification remains deferred to the logger; it does not fix the
module identity at the instant of capture in case of unloading/reuse.

The launcher reads the creation FILETIME on its owned handle before resuming and
publishes it as an exact 64-bit integer. An independent ctypes read verifies it
during the suspension and compares it with the final report. The bounded
[physics-binding](retail_physics_binding_probe.md) collector reuses this
identity, without any native call or reference taken on a Havok object.

After integration, **33/33 x64 CTests and 24/24 Win32 CTests** pass.
No official trial has yet observed these extensions. The results of
trial 20 above remain attached to its former observer binary.

## Private evidence of trial 20

[Trial 21](official_trial_21.md) has since contributed the return address and
its deferred classification in `winmm`, as well as 84 code reads. The figures
below remain those of trial 20, without merging the batches.

Root directory: `<isolated-copies>/slot0/20261030T134824-33340/`.
The commercial files and bytes stay outside Git. The fingerprints of the summary
were rechecked against the files, as were the 67 observed files:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `physics-context.json` | 6 862 | `9d4b443467610847667d90012ad425744a6ada5172d9aa9306bd5c389a354a9c` |
| `documents/observer.log` | 22 320 | `2005f7267d90a77acf42e2926247f46bee1b3e9194d0f06060e21dd284c17f3a` |
| `timing-scalars.json` | 27 695 | `43edeb9ed2bbe6a8c9f9b31663def8b9a6502ed75903faa85ad8f76611a1ef87` |
| `code-helpers/comparisons.json` | 24 201 | `c9afbd889a328c05750f38005629bf2c46e552ac5bf33fb4ea57ea91c28bc382` |
| `startup-report.json` | 2 579 | `1ce53482e1139f54f67644169ee8d14008f14261a431f02441154f872217fc31` |
| `postflight.json` | 1 984 | `999731889460606043a75d754b9fdf27240c3231beb4d94a3f76c2ca974af107` |
