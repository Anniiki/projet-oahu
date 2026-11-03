# Thread dependencies of the physics integrator

## Result of this phase

The first complete ring of direct calls from `81DFD0` has been reread:
27 distinct targets, including 21 new bodies representing 31,065 bytes
of Rea AddressSets. No explicit FS/GS access and no instruction setting the
x87/MXCSR control were found in these bodies. **This does not prove that
their descendants, virtual calls or callbacks are thread-independent.**

The eight direct bodies that differed from the official disk are now
corroborated by the earlier official trial 20. This lifts that
code-provenance limit without establishing their live bindings or the
thread independence of their descendants. The floating-point context
measured at the entry of `81EE50` is also a positive observation, bounded
to those entries; it does not yet validate moving the integrator to any
C++ worker.

The static investigation and this documentation update ran no game,
no runtime probe and no build. The runtime observations below come from
the earlier trial, finished and closed. No code, hook, configuration or
protection is modified by this update. The drain and join protocol
remains that of
[retail_physics_thread_adapter.md](retail_physics_thread_adapter.md).

## Sources, identity and coverage

Official PC MC 1.66 A reference, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`;
Platinum/Paradise secondary, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
x86 VA, base `00400000`.

The `physics-thread-adapter` and `paradise-physics-owner` exports were
reused. The new private evidence is in
`<private-evidence>/reprise-2026-10-30/rea/physics-thread-dependencies/`:
`session/`, `asm/`, `callee-index.json`, `disk-comparisons.json`,
`pe-tls-metadata.json` and `evidence-index.json`. Each new body studied,
selected instruction or table read is compared with both disks over its
exact ranges; AddressSet gaps are not treated as read code.

The Rea catalog of the integrator's callees, output `02`, carries the ID
`ev_ee81003c02ea209f66a378368dfc58581cf03cf7d5c47a87a37a9b6ec1641678`.
The six bodies carried over from the previous phase are `852AA0`, `606B60`,
`606B90`, `84B3D0`, `8E45D0` and `47DA40`.

Among the 27 targets, 17 have identical bodies on both disks.
The lock helpers `606B60/40` and `606B90/47`, which differ on disk,
are already corroborated in official memory by the earlier evidence.
The eight other differing bodies are corroborated in official memory
by `20261030T134824-33340`, PID `29576`, base `00400000`:

| Direct target | Corroborated bytes | Remaining limit |
| --- | ---: | --- |
| `43BD00` | 143 | Math/object descendants not closed |
| `43BD90` | 409 + 144 | Two blocks of the AddressSet; descendants not closed |
| `4767E0` | 40 | Virtual call through the global object `FAC060` |
| `476860` | 48 | Release through a global object; descendants not closed |
| `479330` | 100 | Corroboration of the body, not of its descendants |
| `47DA40` | 158 | Simulation dispatch through `world+8`, then vtable `+8` |
| `4AF750` | 76 | Four direct descendants not closed |
| `5026F0` | 5 | Thunk with a terminal jump: its short body does not close its dependency |

The private evidence is in
`<isolated-copies>/slot0/20261030T134824-33340/`:
`code-helpers/observations.json`, `comparisons.json` and the associated `.bin` files.
The 67 files total 14,863 bytes; their sizes and SHA-256 were
rechecked against the catalog. For the eight bodies above,
`matches_secondary=true` and `own_edits=[]`: no project-made edit is used to
explain the match. Their difference from the official disk remains
recorded; the memory corroboration does not erase it.

For `43BD90`, the exact intervals are `[43BD90,43BF29)` and
`[43BF30,43BFC0)`. They cover the 553 bytes of the Rea AddressSet, including the
`JMP 43BF30` at `43BF27`, but not the seven bytes `43BF29..43BF2F` excluded
from that AddressSet. **This is not a contiguous 560-byte capture.**
The two SHAs correspond to the two blocks of `disk-comparisons.json`.

The absence of FS or FPU control in a thunk is not an observation
of its destination. Code comparisons establish neither branch execution
nor the values of objects in memory.

## TLS: local absence, differing imports, no invented attach

The **static** TLS PE directory has RVA zero and size zero on both
files. This reading of PE metadata is kept separate from the Rea
outputs. It concerns neither DLLs, nor dynamic TLS, nor values
initialized on demand by a library.

The import inventories differ: the official executable exposes `TlsAlloc`,
`TlsFree`, `TlsSetValue` and `TlsGetValue`, absent from the secondary inventory.
Reused evidence: original `02-inspect_pe_imports`,
`ev_3e7c5df8a2b931e7e3be61bdf0296228e88009e9dd39f0ce31aea573b3ce2b57`,
and secondary `89-inspect_pe_imports`,
`ev_c4b22329685fe5056c278dd21ba29814c951531ed5cae7c76d0b392235c94b72`.
These imports are not attributed to Havok without evidence. They are not
followed outside the gameplay bodies corroborated in this investigation. It is
therefore not concluded that the original does not use TLS.

No Havok thread attach/detach point was identified in the first ring
or in the complete historical worker. DLL notifications, CRT TLS and
unresolved callbacks remain outside this bounded absence. The adapter
must not invent a Havok attach function or copy the TLS values of
another thread.

## World initialization and shared allocators

`81D960/1301`, identical on disk, is called during physics init.
It prepares a structure, then calls `47F1D0` at `81DADD` and publishes its
result to `service+865C` at `81DAE2`. This is not an init to repeat at
each wake-up or on each simulation thread.

In the secondary, the constructor `47F1D0/2700` chooses the `world+8` object
according to the configuration byte `+95`. The configuration constructor
`4836D0/213` gives it four; the init code also prepares the value
four on the stack before construction. The table `47FC5C/32` leads this case
to the constructor `4D2370/82`, which installs the vtable `9EFB90`.

This 16-byte vtable is identical on both disks; its slot `+8`
designates `4D46E0/1405`. However, **the constructors, the dispatch table and
the body `4D46E0` differ on disk and are not corroborated here in official
memory**. This secondary path provides verification addresses,
not proof of the player's live binding. `4D46E0` still has virtual
and direct calls: reading it does not close the whole engine.

The global `FAC040` is used by allocation in this construction.
Rea typed references locate its three writes in `475CE0/63`.
In this secondary body, the old object is decremented and may receive
a virtual call `+2C`, then the global is replaced. **This global setter
is not a thread TLS attach.** Calling it again to "initialize the
worker" could change or release the shared allocator. It must not
serve as a substitute for an attach that has not been established.

New evidence: init `47`, constructor `56`, references of `FAC040`
`57`, global setter `60`, configuration `62`, simulation `64`, table/vtable
`65/66`, simulation target `68`. Their full IDs are in the index.

## Concrete FPU precondition

The complete official body of `81DFD0`, identical on disk, computes an
inverse of the delta: `81E04F` contains `DIVSS XMM2,XMM0`, after the multiplication
by `F8A2EC`. The zero-delta path with a finite factor reaches this division
without a guard. The EB/EC flags do in fact allow a call with a null delta.
The SIMD exception masks are therefore materially relevant: enabling
incompatible traps on the new worker can turn a historical path into a
hardware fault.

The computations also use x87, notably for times and profiling.
The x87 control word and MXCSR must be handled separately: masks,
rounding, x87 precision, SSE FTZ/DAZ where relevant. Microsoft describes
this distinction in
[__control87_2](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/control87-controlfp-control87-2?view=msvc-170).

### Entry context observed in trial 20

`physics-context.json` normalizes 32 immutable slots from the private log
`documents/observer.log`. The sizes and SHAs of its four reference files
were rechecked. All these entries of `81EE50` carry
`timer_id=16`, `CWx87=027Fh` and the MXCSR control `1F80h`.
The raw MXCSR is `1F80h` once, then `1FA4h` 31 times: the status bits
are `00h` and `24h` respectively. These persistent bits are
neither a control policy to copy nor proof of their cause.
The observed controls mask x87/SSE exceptions, select round-to-nearest
and 53-bit x87 precision, and leave FTZ/DAZ disabled.
The x87 status and the floating-point register stack were not captured.

The binding measurement at display initialization gives
`readable=true`, `api_available=true`, `matches_timeSetEvent=false` for
the fixed cell `9EC4A8`. The capture of the `timer_id` parameter and the shape
of the callback are therefore not enough to attribute these entries to WinMM. No
target of this cell is followed to explain this difference.

The 26 distinct readings of `timing-scalars.json`, over a window of
5.283 s, show `ED=1`, an integrator entry counter of zero,
frequency 90 and mode one. The readings are not an atomic snapshot
with the FPU captures; the latter have no timestamp of their own. The TIDs
and times of the lines are those of the external logging, after
capture. Neither callback cadence, nor execution on a WinMM thread, nor
the context of an active Havok step is established. This record can guide an
explicit compatibility policy for the future worker; it is not enough
to validate the native call under that policy.

The secondary imports `_controlfp` from `MSVCR71.dll`. Its thunk `675494`
is called by the 18 bytes of `6753B8`, named `__setdefaultprecision`
by the Ghidra analysis: it passes the raw values `10000h` and `30000h`.
The observed references tie it to CRT startup, not to the physics
worker. These two bodies differ from the official disk and have no
memory attestation in this investigation. They therefore provide **neither a
native policy to copy nor the provenance of the measured entry context**.
The CRT entry body is not analyzed further.

`_beginthreadex` governs the CRT startup of the reconstruction's worker; it does
not prove equivalence with the floating-point context and the private data of
`MSVCR71.dll` used by the game. Before a native integration, an explicit
FPU policy must be justified by the state observed on the real physics
path, then applied and checked on the worker. No state is inferred
from the renderer's, from a universal preset or from the absence of a local `FLDCW`.

## Callbacks and preconditions useful to the adapter

The retained ring contains dynamic boundaries: for example
`83DE83` calls the global pointer `169A8B0` if it is non-null, with argument
two. `837CD0` and `83DB70` also perform several virtual calls; the
two virtual calls of `81DFD0` are not the only ones in the subgraph.
Their affinity, reentrancy and lifetime cannot be
inferred from the absence of TLS in the calling bodies.

Before any real hookup:

1. Wait for a complete and stable native initialization of the service, of
   `service+865C`, of its simulation object and of the shared allocators.
   Do not recreate these objects for each worker.
2. The eight direct bodies are corroborated by trial 20 within the limits
   stated above. The binding actually used remains to be established:
   `service+865C`, its `world+8`, its vtable and the slot `+8`. A vtable
   identical on disk does not prove that the live object carries it;
   trial 20 does not corroborate the constructors and `4D46E0` mentioned above.
3. Establish and check the compatible x87/MXCSR policy, including the
   null-delta path, before any integrator call from the reconstruction's worker.
   Distinguish the observed entry controls `027Fh/1F80h` from the accumulated
   statuses and from the active integration context that remains to be qualified.
4. Qualify any thread attach with positive evidence; do not
   call a global allocator setter as an assumed attach.
5. Keep a single owner, the native locks and the drain/join
   protocol; no wait on the worker from itself or under a lock
   it must take. Examine the admitted dynamic callbacks before
   promising the absence of reentrancy into the cleanup.
6. Keep contexts and objects alive until the real thread exit.
   A C++ exception in the reconstruction does not cancel a native effect; `/EHsc`
   provides no general recovery from SEH or Havok locks.

The next native verification must address this live simulation binding
and its stability until the drain, then the positive thread/FPU
obligations of that target. Code corroboration creates neither an implicit
TLS policy nor a need to explore an unbounded graph.
No new probe or dedicated trial was created
or launched in this phase. The single Rea session executed 68 operations,
kept 28 new assembly listings and 44 range comparisons. The bundle and
the snapshot were exported, then the session closed successfully.
