# Bounded observation of the live physics binding

## Result and scope

The existing exports are sufficient to establish the chain to observe, without
a new Rea session: static object `S=01293880h`, world pointer at
`S+865Ch`, simulation object at `world+8`, vtable of that object, then the
target of slot `vtable+8`. **`S` is not a cell holding a service
pointer.** Its first DWORD is a vtable, not the address of the service.

The collector and its external Windows adapter are now written and
verified on source-owned fixtures. They have not been run against the
game. They will be able to characterize an observed binding, without keeping the
objects alive or authorizing an integrator call. The
thread/FPU, single-owner, drain and join obligations
remain those of the
[dependency](retail_physics_thread_dependencies.md) and
[adapter](retail_physics_thread_adapter.md) studies.

## Identity and machine provenance

x86 VA addresses, expected base `00400000h`, four-byte pointers and
DWORDs, little-endian. Official PC MC 1.66 A reference: SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Studied secondary:
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.

| Step | Instructions or data retained |
| --- | --- |
| Service anchoring | `97CBBA` loads the immediate `01293880h` into ECX; `97CBBF` calls the registration `9B4900`. The callback also pushes this immediate at `81EFA4`, before its call to `81DFD0` at `81EFA9`. |
| Candidate service identity | Table `F27068/32`, identical on both disks: slot `+8=84D570` for init; `+1C=846CF0` for cleanup. The value actually held at `S` remains to be measured. |
| World publication | `84D570` keeps ECX in EDI at `84D57D`, then passes the service to `81D960`. This routine calls the constructor at `81DADD` and writes EAX to `[ESI+865C]` at `81DAE2`. |
| World consumption | In the integrator, `81E575` loads `[EDI+865C]` into ECX; `81E57C` calls `47DA40`. |
| Simulation dispatch | `47DA46` keeps ECX in ESI; `47DA48` loads ECX from `[ESI+8]`; `47DA4B` loads EDX from `[ECX]`; `47DA50` calls `[EDX+8]`. |

The callback anchoring belongs to the body `81EE50/596`, identical on both
disks. `81D960/1301`, the complete init/cleanup bodies and the integrator
body are identical as well. The dispatch `47DA40/158`, which differs on
disk, matches the secondary in the official memory of trial
20, with no edit of this range by the project. This does not reveal the live
pointers it would have used.

Private evidence reused under
`<private-evidence>/reprise-2026-10-30/rea/`:

- `paradise-physics-owner/61-read_bytes.json`: init anchoring in
  `97C8F0`, ID
  `ev_8d823da0a8d05d36c0657457d0bd3306fcc4c4f794f7e23a9e3ebe6fd17ef2f7`;
  `81-read_bytes.json`: the twelve bytes around the callback push/call.
- `paradise-physics-owner/47-read_bytes.json`,
  `targeted-disk-comparisons.json`, `physics-initialize-complete.asm.txt`
  and `physics-stop-cleanup.asm.txt`: vtable, init and destruction.
- `physics-thread-dependencies/session/47-procedure_assembly.json`:
  `81D960`, ID
  `ev_8b5b8fca5a8d91a0bd5612da339fab721a4ce143a41cdf1df94352b72c92acac`;
  `asm/0047f1d0.txt`, `asm/004d2370.txt` and `disk-comparisons.json`:
  construction candidates, with their explicit disk differences.
- `physics-thread-adapter/normalized-09.asm.txt` and
  `integrator-complete.asm.txt`: exact dispatch and caller.
  The corresponding official capture is
  `<isolated-copies>/slot0/20261030T134824-33340/code-helpers/physics_world_dispatch_47da40.bin`.

## Exact read sequence

The profile is compiled into the probe, reserved for this identity and this
base. No option for an arbitrary address, displacement, length, depth or
vtable target. No memory scan, pointer search,
target function call or recursive walk is necessary.

| Order | Read | Size | Handling of the result |
| --- | --- | ---: | --- |
| 1 | `[01293880]` | 4 | `service_vtable`; the first profile requires `F27068`. |
| 2–3 | `[F27068+8]`, `[F27068+1C]` | 4 + 4 | Require `84D570` and `846CF0`, in code pages of the expected module. |
| 4 | `[0129BEDC]`, that is `[S+865C]` | 4 | `W=world`; zero means observed absence, with no continuation. |
| 5 | `[W]` | 4 | World vtable; secondary candidate `9ECE38`. |
| 6 | `[W+8]` | 4 | `P=simulation`; zero stops this reading. |
| 7 | `[P]` | 4 | `V=simulation_vtable`; bounded candidate `9EFB90`. |
| 8 | `[V+8]` | 4 | `F=step_entry`; candidate `4D46E0`. Never call it. |
| Context | `[0129BEED]`, that is `[S+866D]` | 1 | Raw ED value, without treating it as a lock or a generation. |

The secondary constructor `47F1D0` places `9ECE38` in the world. Its
configuration case four builds a simulation through `4D2370`, which places
`9EFB90`. The 16 bytes of that last table are identical on both
disks; their slot `+8` holds `4D46E0`. On the other hand, the constructors,
the selection table `47FC5C` and the body of `4D46E0` are not corroborated
by trial 20. **These values define the candidate to verify, not an
official binding already observed.** Any other value produces
`unsupported_binding` and stops the walk; no other slot or module is explored.

At most two identical passes give a budget of 66 bytes of target
data, excluding identity verification and page metadata. No automatic
retries in pursuit of a stable result. On the second pass, start again
from the fixed cell; never reuse an old W/P without revalidation.
Keep the two results and the read interval separately.

## Read guards and classification

The probe must target only the child process owned by the
launcher, through a retained handle, matching PID and creation time. It
verifies the official SHA of the locked copy, PE32/I386, the main module
base and the bounds of its image. A PID alone or a file name is not
enough. Reads use a query/read-memory handle,
with no write, suspend or injection right.

Before each read, compute the address and end in a widened integer; refuse
zero, misaligned pointers, x86 overflow and ranges outside the accessible
user space of the target. Check each region covering the
four bytes, then require an exact read: `MEM_COMMIT`, readable
protection, without `PAGE_GUARD` or `PAGE_NOACCESS`. The prior page check
does not prevent a later change; a failed or partial transfer is
`unreadable`, never a usable partial value.

- S and its `+865C` cell must belong to `MEM_IMAGE`, an allocation of the
  identified main module, in non-executable data pages.
- W/P are allocated objects: do not impose a fixed address on them or
  infer their allocation size from a DWORD. Accept for this profile
  only `MEM_PRIVATE` data pages, or `MEM_IMAGE` pages of the same module,
  readable and non-executable; otherwise stop without widening the search.
- Candidate vtables must be aligned, at the expected addresses only,
  in `MEM_IMAGE` of the main module, in readable, non-executable,
  non-writable pages. Refuse an unknown binding before following its
  slots. The page holding F must belong to the executable code
  of that same module; a plausible address does not attest its body.
- The reader neither reads nor modifies the `+4/+6` reference counters,
  takes no game lock and triggers no virtual method.
  Diagnostics are produced by its own thread, outside any callback.

The outcomes must remain distinct: identity refused, observed absence,
page/read refused, unsupported candidate, differing values
between passes, concordant candidate. "Concordant" does not mean "alive
until the next tick" or "ready to be called".

## Known writes, replacement and destruction

| Routine | Effect proven in the exports |
| --- | --- |
| `81D960`, during `84D570` | Publishes the constructor result into `service+865C` at `81DAE2`, before the lock acquisition at `81DAF5`. No publication acknowledgement usable by the probe is inferred from it. |
| `47F1D0`, secondary constructor | Writes zero to `world+8` at `47F3F0`, selects/builds the simulation, then writes its pointer at `47F54D`. The virtual release branch `47F42D` exists after this reset; this body does not demonstrate a hot-replacement API in a loaded world. |
| `846CF0` | Destroys many objects as soon as it is entered. At `847149`, reloads `service+865C`, decrements its 16-bit counter `+6` if `+4` requires it, and may call its virtual slot zero at `847169`. **The reset of `service+865C` only happens at `847173`.** The pointer may therefore still be non-null after a release. |

The actual destructor reached through the world's slot zero and its descendants
are not established here. This list describes the writers/destructors
known in the exports; it is not an exhaustive analysis of the
aliases, grouped writes or callbacks able to change these objects.

## What a reading can and cannot establish

Even aligned four-byte reads do not form a transaction
over the whole chain. The world may be destroyed between W and `[W+8]`; the
memory may remain mapped and hold an old vtable. Two equal passes
do not exclude ABA: destruction then reallocation at the same place,
or even reuse of the same addresses for W/P and of the same vtables. Page
checks do not extend the C++/native lifetime of the objects.
An equal ED in both passes establishes neither the absence of cleanup, nor an
immutable generation, nor a drained integrator.

A concordant candidate will therefore make it possible to target a future
corroboration of the body actually designated and of its positive thread/FPU
obligations. Its use by the adapter separately requires an initialization
barrier, an owned generation, the exclusion of binding/mode changes, then
closing admission and draining before any destruction, followed by the
actual join. A pointer equality replaces none of these steps.

## Delivered interface and launcher wiring

The core, in standard C++ (`/std:c++latest`), is in
`include/tdur/diagnostics/physics_binding.hpp` and
`src/diagnostics/physics_binding.cpp`. The call
`collect_physics_binding(reader, {pid, creation_filetime})` receives a
`PhysicsBindingReader`: verified identity, QPC timestamp, page
description and 1/4-byte transfer. The adapter must tell the truth about these
facts; injected fixtures do not constitute an official identity.
The core applies the bounds, page roles and candidate values, stops
at the first refusal and allocates no internal storage. Its queries
go through the injected reader; it never calls the target code.

The result keeps two passes, their optional values, raw ED,
QPC intervals, requested/received bytes and the refusal field. The statuses
distinguish in particular `absent`, `unsupported_binding`, `page_refused`,
`unreadable`, `identity_refused`, `timestamp_refused`, `changed` and
`candidate_concordant`. A partial read never fills a
typed value. No retry follows a refusal, even after a complete first
pass. The intervals concern the observer, not a physics step.

`src/runtime_physics_binding_probe_main.cpp` provides the CLI:

```powershell
tdur_probe_physics_binding.exe --pid 1234 `
  --expected-creation-filetime 134378417126074006 `
  --expected-exe "<private-path>\TestDriveUnlimited.exe" `
  --output "<private-evidence>\physics-binding.json"
```

The PID and the FILETIME must come from the same owned child. The
project-owned launcher publishes `creation_filetime` in `suspended-pid.json` and
`startup-report.json`, through its handle before resumption. Pass this raw
integer `(high << 32) | low`, without conversion to a date, seconds or a
float, and without a round trip through a JavaScript number. The CLI
verifies the FILETIME on its two read handles before any binding read,
then rechecks it around each pass.
It does not prove on its own that the arguments come from the launcher: this
provenance remains the responsibility of the project-owned wiring.

The CLI locks the executable with `FILE_SHARE_READ`, verifies the official
size/SHA through `inspect_installation`, its match with the process,
the mapped PE32/I386 headers, base and size. It composes
`src/runtime_readonly_image.hpp` **without modifying it** for identity and
headers. A second query/read handle, at the same FILETIME, serves
the image and heap payloads after revalidation of their pages; the count
transferred by `ReadProcessMemory` is kept even on error. The PE identity
data requires at most seven DWORDs, counted
separately in `identity_pe_header_bytes`, outside the binding budget of 66.
The result file must be new. An observed refusal is a diagnostic
report with its status; an admission/argument/output failure sets
the process exit code to one without producing a valid binding report.

The JSON sets `atomic_snapshot`, `object_lifetime_validated`, `aba_excluded`
and `native_call_authorized` to false. It exposes no address, profile
or pass-count selector. The separate `--self-test` mode takes
no PID: it uses exclusively the process of its own source-owned
executable, with official admission disabled.

## Private validation performed

Private evidence and binaries:
`<private-evidence>/reprise-2026-10-30/physics-binding-cpp/`:
`build-private.cmd`, `build-test.log`, `build-probe.log`,
`cli-refusals.json` and `validation.json` with SHA-256 of the four sources
and of the CLI. Sequential MSVC 14.44 compilation, Win32, `/std:c++latest`,
`/EHsc /W4 /WX /permissive- /O2`; no warning option weakened.

```powershell
physics_binding_test.exe
tdur_probe_physics_binding.exe --self-test
```

- **3,552 core checks**: exact order and budget of the reads,
  no access to the `+4/+6` references, initial and changing identities,
  absence/unknown values without continuation, page permissions and types,
  x86 `world+8` overflow, failed/partial transfers at each of the 18
  positions, failing/backwards timestamps, differing bindings/ED.
  One fixture recycles the same addresses and still produces a concordance:
  it makes the ABA limit explicit, without claiming to detect it.
- **28 real Windows checks**: 32-bit pointers in the image/heap, image/vtable
  permissions, `PAGE_NOACCESS`, page boundary, `PAGE_GUARD` preserved
  after refusal, executable page refused as an object, unchanged canaries,
  stale FILETIME and exact int64 parsing. Only the fixture's own pages are
  allocated/protected/written; no commercial process is
  targeted. The fixture does not simulate an official admission of the binding.
- **Nine CLI refusals**: arbitrary address, missing/extra parameters,
  invalid PID or FILETIME, non-official source-owned image; no evidence
  file created. The CLI does produce a PE I386 image.

These numbers count fixture assertions, not game scenarios.
No game, hook or Rea session was involved. The two previous studies
remain unchanged. The launcher wiring and any official observation remain
to be done at integration.

## Integration

The core is built in the main repository in x64 and in the Win32 bench.
After review, an undercount of partial image transfers was corrected:
the direct read on the same-creation handle now keeps `SIZE_T`
on error for all payloads. The core also checks `false/short` at each
stage, separately from `false/0` and `true/short`: **4,551 checks pass
in each architecture**. The **28 Windows checks** pass again.

The wrapper accepts `--binding-probe PATH` and passes the PID and the
`creation_filetime` of the file published by the launcher during suspension,
as exact decimal integers. It refuses a launcher without this identity,
keeps the probe SHA at preflight and places its result/its command in
the private batch. No additional profile or address is exposed. An
in-game observation remains to be performed; these validations are source-owned.

Integrated build: **33/33 x64 CTests and 24/24 Win32 CTests pass**,
after explicitly building all executable targets. The logs
`integrated-root-build-v2.log`, `integrated-mod-build-v2.log` and their
`integrated-*-ctest-v2.log` are kept in the private batch above.

## First official result

[Trial 21](official_trial_21.md) runs the probe with the exact creation
identity. It refuses before any payload after 28 PE bytes, status
`identity_refused`. The separately observed integrator counter advances,
but no binding value is available. The next diagnostic must
keep the seven DWORDs already received, the PE boolean and the user ceiling
already acquired, without any additional read or relaxed admission.
The exact cause remains open.

The report has since been enriched with the seven PE words already acquired
(RVA/value), the user ceiling and the verification booleans. The cache is
bounded to 28 bytes; the identity check and all page rules remain
identical. The source-owned fixture verifies each cached word against
its own image: **36 Windows checks**, two targeted CTests pass.
This extension has not yet been observed on the official executable.

## Diagnostic resolved and binding observed

[Trial 22](official_trial_22.md) keeps the words: the runtime reports
`012AD000`, while Toolhelp keeps `01BB0000`. The equality imposed by the
old adapter explains the refusal. The fix requires the exact measured runtime
tuple, separately from the loader, the base and the locked official SHA.
It follows no unknown e_lfanew and bounds MEM_IMAGE to the smaller runtime.
The fixtures pass 4,700 core checks and 97 Windows checks.

[Trial 23](official_trial_23.md) performs two complete concordant passes,
**66/66 bytes**, expected service/world/simulation and vtables, ED0. No
permission, identity, transfer or budget guard is removed.
This successive read still establishes neither lifetime, atomic snapshot,
absence of ABA, thread affinity nor permission to call the physics engine.
