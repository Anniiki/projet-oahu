# Physics adapter on the native thread: preliminary study

## Conclusion and scope

The preferred path is a **single cooperative C++ worker on the thread created
by the native helper `606F20`**, with `FixedPhysicsTimer` and a waitable timer.
It avoids running the schedule, the CRT and the integrator inside a WinMM
callback. This document specifies an architecture to be verified; it delivers
no hook, no Havok wiring and no validation in a loaded world.

Two conditions prevent a naive substitution: the drain must precede entry
into the cleanup `846CF0`, and **writing `stopState=2` does not prove that the
thread has terminated**. A join on the real handle remains necessary.
The priority applied by the parent thread after `CreateThread` must also be
handled before integration is allowed on this worker. Official trial 20
now corroborates its dispatch table and the eight direct boundaries
listed in [the dependency study](retail_physics_thread_dependencies.md).
Live bindings and the context of an active integration step remain
preconditions; no native wiring is validated by these captures.

This conclusion replaces the preference for a direct substitution of the
callback `81EE50` expressed in [the earlier study](retail_physics_owner.md).
The historical observations of that study remain valid.

## Identity and evidence

Addresses are x86 VAs, base `00400000`.

- Official PC MC 1.66 A reference: SHA-256
  `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
- Platinum/Paradise secondary: SHA-256
  `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.

The new private evidence is in
`<private-evidence>/reprise-2026-10-30/rea/physics-thread-adapter/`:
`session/`, `disk-comparisons.json`, `evidence-index.json` and the bounded
assemblies. No commercial byte is added to the repository.

| Range | Established result |
| --- | --- |
| `81DFD0..81EE44`, 3 701 bytes | Complete integrator body, identical in both files on disk; SHA `15d05060f6cf7d23fdff8521434c156bb8f4c1aa92e0f0d1f475e96f06a6063c` |
| `81F0B0/223`, `84D570/2276`, `846CF0/1263` | Worker, init and cleanup identical in both files on disk |
| `852AA0/88`, `8E45D0/541`, `9B4560/58` | Time, update and dispatch helpers reread; private comparison of the exact ranges |
| `606F20/70`, `607010/73`, `606B60/40`, `606B90/47` | Differ on disk; official/secondary memory equality already established by closed trial 16 |
| `606F70/128` | Differs on disk; corroborated in official memory by closed trials 18 and 19 |
| `606FF0/16`, `47DA40/158` | Differ on disk; official/secondary memory agreement established by closed trial 20, without editing these ranges |
| `607000/5` | Differs on disk; no official memory corroboration of this range is inferred here |

The new Rea read of the integrator covers 3 700 bytes (`02`) and its
last byte (`05`); their concatenation equals exactly the 3 701 bytes
of both files on disk. The final `RET 0Ch` is at `81EE42` and ends at
`81EE44`.
Ghidra describes an AddressSet of 3 698 bytes, excluding three NOP bytes
at `81E15D..81E15F`; the check of the full span includes them.

Main evidence:

- integrator bounds, `12`:
  `ev_cc8f3ba7bc6e403ca9fc837bb08aa34488a6e8e82e38cbbd831ebf5e7a491eb2`;
- complete assembly, `13`:
  `ev_4615750fb6a82287b5b98d1f88812648ac34f7d798e463141d2ba0f75ed1fc32`;
- complete raw worker, `27`:
  `ev_56fce3de403ee7f2fcee68309e3fafa506f04fb7e7adb99896c52df20f81c844`;
- complete raw cleanup, `28`:
  `ev_52964f84947b4df4f259475ae6f63020cb0ff121b8e6ba5a86795f02a6c25e4e`;
- termination helper, `21`:
  `ev_1a7f130618ca0375e5706eb798153e2554c1cddb30a327e5a29ce32f717cf3bb`.

These are static observations and comparisons with earlier captures,
not an execution observation of the complete integrator.
No protection mechanism is studied or modified.

Runtime complement (official trial 20): trial `20261030T134824-33340`,
PID `29576`, official reference verified and locked. The private folder
`<isolated-copies>/slot0/20261030T134824-33340/` keeps
`code-helpers/{observations,comparisons}.json`, the captured binaries,
`physics-context.json`, `timing-scalars.json` and the logs. The sizes
and SHAs of the 67 captures, 14 863 bytes in all, were rechecked. The eight
physics boundaries and the priority table have `own_edits=[]` and
agree with the secondary. For `43BD90`, there are two blocks
`[43BD90,43BF29)` and `[43BF30,43BFC0)`: the 553 bytes of the AddressSet
are covered, but neither the seven excluded bytes `43BF29..43BF2F` nor a
contiguous span of 560 bytes. The post-check reports 6 697 reference files
and 17 saves unchanged; the slot is released. This update uses
these existing files, without a new trial or code change.

## Why move integration out of the WinMM callback

The primary [TimeProc](https://learn.microsoft.com/en-us/previous-versions/dd757631(v=vs.85)) documentation
limits the callback's system calls to eight notification, time, MIDI and
diagnostic functions. QPC, waits, locks and the general CRT have no such
contract. The calls already present in the game do not justify adding
a more complex C++ owner there.

On an ordinary thread, a cooperative wait allows QPC, a stop event and a
one-shot timer, without an APC callback. Deadlines are rational at 90 Hz;
an integer 11 ms period would produce about 90.909 wake-ups/s. The wake-up is
not the physics step: the schedule keeps the time actually accepted,
including delays, and passes `1/90`.
The APIs guarantee no exact punctuality.
See [SetWaitableTimerEx](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setwaitabletimerex)
and [WaitForMultipleObjects](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects).

The QPC/deadline/100 ns conversions must remain checked in integer arithmetic.
The stop event has priority in the wait; no handle is closed while a thread
may still be waiting on it. The budget of one notification does not, by
itself, bound an outer catch-up loop: that loop must recheck the stop and
the flags between steps, without discarding the debt.

## ABI, generation and worker bootstrap

`84D990` pushes `81F0B0`, then `84D997` calls `606F20`. The wrapper passes
`CreateThread` the service as parameter, a `40000h` stack, zero flags
and an identifier pointer. It stores the returned handle at `606F5A`.
The thread can therefore start **before** this store and before the end of init.
The [CreateThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread) contract
confirms this possibility.

The proposed admission point is a targeted hook of this public call:
`lpStartAddress==81F0B0`, direct return `606F5A`, matching parameter and
initialization context. Other creations pass to the original.
The immutable context, the events and the generation must exist
**before the real call**. The pointer to this context replaces the parameter
of the single admitted worker; it keeps the original service. A 64-bit token
must be neither truncated to an x86 `DWORD_PTR` nor reread from a current
global token.

The source entry must respect `DWORD WINAPI(void*)`, hence a 32-bit
argument and stdcall cleanup. The historical worker loops without returning:
its body alone provides no `RET4` epilogue to copy. The ABI comes from
its use as `LPTHREAD_START_ROUTINE`. The integrator separately keeps
`void __stdcall(void* service, float delta, float argument2)` and
its two `RET12` exits.

A startup rendezvous is needed to publish the handle and let native init
finish before integration. `ED=1` during init, then its zero write at
`84DE39`, do not replace this publication protocol.
A partial creation/init failure must keep admission closed.

Do not call the original worker after substitution: it would still create
the WinMM timer. Its preamble does, however, publish scalars that other
routines use:

| Field written by the worker | Attested consumer |
| --- | --- |
| `168D6B4`, float period | `8DC0E1`, SSE read |
| `168D6B8`, float period | `825B20`, x87 division |
| `F8A2E4`, float frequency | `825B47`, x87 read |

Evidence `24..26` establishes these reads. Their complete semantics are not
invented. A substitution must preserve this consistent publication,
check the 90 Hz configuration and examine the other QPF writes of the
preamble, even if the new schedule has its own QPC frequency.

### Priority applied after creation

Init calls `606F70` at `84D9B6`, after `CreateThread`: argument two
in mode one, three in mode two. The table `606FF0` selects
`606FB6` and `606FCB` respectively, that is, the `SetThreadPriority`
arguments `-15` and `+15`. Microsoft names them
[IDLE and TIME_CRITICAL](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadpriority).

The 128-byte body is corroborated in official memory, SHA
`bbdc8dcbca33d4095679e2e07a8a3d953f625bd3f8a2931fb9227e719565bb50`,
by `20261030T121220-31820/code-helpers/comparisons.json`. **The 16-byte
table lies outside these 128 bytes and differs on disk**; it is
now corroborated separately by trial 20, SHA
`7ff834c21d6f11d097393c4b7f1e3ee12d1e846d060f8e353702430cbfa4337b`.
Its four DWORDs are `606F8C`, `606FA1`, `606FB6`, `606FCB`: indices
zero/one/two/three, branches that pass `+2`/`0`/`-15`/`+15`.
This mapping therefore does exist in the captured official memory. This
proves neither the execution of each branch, nor the success of
`SetThreadPriority`, nor the effective priority of the thread.

A future worker that actually integrates physics must not blindly inherit
a priority intended for a thread that slept. An explicit NORMAL policy
must be applied and verified after the parent thread's setting, not only at the
entry of the new thread. The helper `607000`, called with four, is a stub
returning false in the secondary; no CPU affinity is inferred from it,
and its official body in memory remains to be verified.

## Complete integrator body: locks, TLS and FPU

The 874 instructions of the Rea AddressSet contain 35 direct calls,
two virtual calls and two QPF/QPC IAT calls. The full span was also
disassembled with MSVC `dumpbin` 14.44.35229.0.

- `81DFE2` calls `852AA0`, the QPF/QPC time helper, before the lock.
  The counter `16A69F0` is also incremented before this lock.
- `81E003` enters the lock `service+87D0`, released at `81ED81`.
  Final profiling then continues until the return: releasing the lock
  therefore does not mean leaving the function.
- Two branches also take the lock `129C050` at `81E2F2` or `81E332`,
  then release it at `81E309` or `81E349`. The observed order is the
  service lock, then this secondary lock; a caller taking them in the
  reverse order could prevent a drain. The absence of such callers
  is not demonstrated.
- Under the main lock, `81E57C` calls `47DA40`, which performs virtual
  calls. Its 158-byte body, which differs from the official file on disk,
  is now corroborated in official memory by trial 20. Its live virtual
  targets are not established by this code equality.
- The virtual calls `81E3F1` and `81EB27`, as well as the call `81ED2F` to
  `8E45D0`, open other paths. `8E45D0` itself calls the virtual slot
  `+8` at `8E4645`. Their non-reentrancy with respect to the cleanup
  and their thread affinity are not proven.

In **this complete body**, no FS/GS instruction, explicit TLS access,
MXCSR or x87 control read/write, or FPU initialization is observed.
It does, however, use SSE and x87. The complete worker contains no
explicit TLS/FPU setup either. The
[first ring of dependencies](retail_physics_thread_dependencies.md)
extends this reading to the 27 distinct direct targets; this bounded absence
does not extend to their descendants, to virtual calls or to DLL
notifications.

The [TLS slots of a new thread](https://learn.microsoft.com/en-gb/windows/win32/procthread/thread-local-storage)
do not copy another thread's private values. No equivalence between
the WinMM environment, the worker and the main thread is asserted.
The Havok boundaries and the x87/MXCSR environment remain to be verified
before a real transfer; do not arbitrarily import the renderer's.
Microsoft distinguishes these controls on x86 in
[__control87_2](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/control87-controlfp-control87-2?view=msvc-170).
The compatibility of CRT usage on the thread created by `CreateThread`
must also be verified; keeping this handle does not provide a
`_beginthreadex` CRT startup.

### Measured entry context, without attested integration

Trial 20 keeps 32 entry captures of `81EE50`, all with
`timer_id=16`, x87 control `027Fh` and MXCSR control `1F80h`. The raw
MXCSR equals `1F80h` once and `1FA4h` 31 times; the status `24h` is not
a configuration to copy and its cause is not identified. No x87
status, timestamp or thread identifier is captured in these slots.
The times and TIDs of the lines come from their later logging.
This measures neither cadence nor step duration.

The external diagnostic of the binding `9EC4A8`, at display initialization,
is readable and finds the export `timeSetEvent`, but the equality is false.
These 32 entries therefore cannot be qualified as attested WinMM callbacks.
The 26 separate timing samples, over 5.283 s, show `ED=1`, an entry
counter of `81DFD0` at zero, frequency 90 and mode one; they are not
atomic with the captures. No active Havok step is validated by this
trial. The observed controls form a compatibility reference,
to be distinguished from an explicit policy, tested and verified on the
future worker before its first native call. Neither persistent statuses nor
another thread's TLS may be copied as initialization.

## Drain before destruction and real join

`846CF0` writes EC/ED to one at `846D00/846D07`, then destroys arrays,
vehicles and references. The object array traversed by the integrator
is destroyed before `84700D`. The `stopState=1` write only comes at `847102`.
The two acquisitions of the physics lock in the cleanup, at `84702C` and
`84708D`, are also later than many destructions. Waiting
only for this lock or for the write of one would be too late.

The proposed protocol, to be validated by fixtures, is:

1. At the targeted cleanup entry, close admission for the generation, then
   wake the worker. Wait for the end of any integrator call, including
   with a zero delta, **before calling the original cleanup**. Do not hold
   a lock needed by the worker during this wait.
2. The drained worker stays alive in a passive state. It no longer reads
   vehicles, no longer integrates and waits only for lifecycle commands. A join
   at this stage would be a deadlock if it is still waiting for the native
   write of one.
3. The original cleanup can then destroy its resources and write one.
   The worker acknowledges two only after observing this write,
   with a Win32 compare-exchange that replaces exactly one with two.
   Do not pre-write two, which the original would overwrite with one.
4. The worker terminates. **The store of two precedes its return; the cleanup
   can therefore overtake it before the end of the epilogue or of DLL
   detachment.**
5. The targeted gate of `607010` establishes the join on its HANDLE before
   any final close/destruction. The expected site is `84712C`, return
   `847131`, with `ECX=service+8798` and the handle at `service+87A0`.
6. For this single owned generation, a C++ branch can wait for the end,
   close the handle and clear the fields `+8`/`+4`, keeping its
   thiscall ABI and AL result, without `TerminateThread`. Other objects
   pass to the original helper. Joining then calling the original would
   also avoid killing an active thread, but would still make it request
   a useless termination.

The native helper `607010` calls `GetExitCodeThread`, then `TerminateThread`
when that read succeeds; it does not test `STILL_ACTIVE` and does not
join. Its targeted replacement must be armed before the worker is created,
outside the loader lock after validation of the runtime body, with the
appropriate synchronization: this helper is shared by other threads.

The handle/generation identity, the success of the wait and its lifetime
must be checked. A cleanup request coming from the worker itself
must never wait on its own handle. `9B4560` performs the reverse virtual
dispatch without taking a lock in its body; this does not prove
that all its callers or callbacks enter without a lock. The drain and
reentrancy remain preconditions to establish, not a property of
`FixedPhysicsTimer`. A failed wait does not allow continuing a
destruction whose safety depended on the drain.

## Single ownership and alternatives

The mode-zero entry of `84DE60` also calls `81DFD0`. A one-time test of
mode one at startup does not close this path: the configuration can write
zero afterwards. The adapter must serialize or refuse mode transitions
during its ownership, or perform a drained transfer. A simple reread
of `10E765C` between two steps leaves a race. No second integrator or
old WinMM callback may remain admitted for the generation.

The C++ worker and the waitable timer are preferred here. Another documented
possibility is [TIME_CALLBACK_EVENT_SET](https://learn.microsoft.com/en-us/previous-versions/dd757634(v=vs.85)):
WinMM directly signals an event the worker waits on. This is not
an application call to `SetEvent` from `TimeProc`. This variant nevertheless
keeps the WinMM stop cycle and its lifetime obligations; it
resolves neither the drain before destruction nor the join.

Rebuilding only the cleanup handshake in C++ would also allow joining
before acknowledging two. That would touch the native flow more than the
targeted gate of `607010` and does not remove the need to close admission
before the first destructions. The adapter concerns physics; by itself it
does not make the game AI or the counters independent of FPS.

## Validation still required and closure

Before wiring: x86 fixtures for the three ABIs, startup before
`CreateThread` returns, late parent priority setting, old generations,
stop in the middle of a step, reentrant cleanup attempt, mode changes,
one/two acknowledgement and a worker deliberately delayed **after** the
store of two. This last fixture must verify that the cleanup stays
blocked on the join. Creation/wait/close failures and the lifetime of the
contexts must remain explicitly covered.

The next concrete native precondition is the live binding of
`service+865C` to `world+8`, its vtable and its slot `+8`, with their stability
during admission/drain. Trial 20 corroborates the dispatch `47DA40`, not the
secondary constructors nor the expected target `4D46E0`. The thread/FPU
obligations of the path actually retained must then be qualified,
without inventing a TLS attachment. The corroborated priority table does not
remove the need for the mode zero/one arbitration, the drain before destruction
or the join of the real handle described above.

The [FixedPhysicsTimer](fixed_physics_timer.md) model has separate standalone
validations. Its handled exceptions are **C++ under `/EHsc`**: no
recovery of Havok SEH, no rollback of the native world and no guaranteed
release of foreign locks after such a fault are promised.

The Rea session reused the existing snapshot. The 36 retained calls
include three assembly requests without an attributed function (worker,
cleanup, caller `97BF20`); their failures are kept and the worker/cleanup
are supported by raw reads and bounded assemblies. The last optional
batch on the mode setter was rejected locally for a non-array JSON,
before the Rea call. The `finally` block nevertheless **exported the bundle
and closed the session with a snapshot**; both results are kept.
No game, build, hook or source file was modified or executed
during this investigation.
