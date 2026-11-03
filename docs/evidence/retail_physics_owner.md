# Physics owner, WinMM cycle and hook point

## Conclusion

The priority hook to study is the substitution of the **single WinMM
callback `81EE50`**, installed during nominal graphics startup, before the
physics worker is created. The historical timer then remains the producer
of notifications; a C++ scheduler can own the sub-step decisions.
This retained-debt policy would be new behavior, distinct from the
historical callback reconstructed in
[legacy_physics_timer.md](legacy_physics_timer.md).

The synchronous mode `84DE60` must remain excluded from this simultaneous
ownership. The native shutdown is not a simple drain to call in order to
perform the handover: it tears down the service, waits for an
acknowledgement, then calls a thread termination helper now corroborated in
official memory. No hook, injected accumulator or loaded-world trial is
delivered here.

## Identity and level of evidence

x86 VAs, preferred base `00400000`:

- Official PC MC 1.66 A: SHA-256
  `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
- Secondary Platinum/Paradise: SHA-256
  `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.

The existing exports `paradise-focus-follow`, `paradise-simulation-dispatch`
and `paradise-video-defaults` were reread before the new Rea session.
The new private evidence is under
`<private-evidence>/reprise-2026-10-30/rea/paradise-physics-owner/`:

| Rea outputs | Content |
| --- | --- |
| `02..08`, `20..38` | References, shutdown, integrator calls, bounded bodies |
| `39..50`, `72..83` | Mode, vtable, flags, control instructions and ABI |
| `51..71` | Registration, scene calls and world constructors |
| `84..86` | Service teardown dispatch |

Ghidra 12.2 does not recognize several bodies as attributed functions.
The Rea inspections/reads are supplemented by bounded MSVC `dumpbin`
14.44.35229.0 disassemblies. The absence of an xref is not enough to exclude
a call: `81EFA9` is not in the Ghidra listing, but its bytes and its
disassembly establish the call. The private `.text` scans provide
candidates; aliases and unresolved indirect calls remain a limit.

`targeted-disk-comparisons.json` retains each exact range and its SHA.
Matching across the disks: physics init `84D570/2276`, teardown
`846CF0/1263`, worker `81F0B0/223`, callback `81EE50/596`, integrator
entry `81DFD0/112`, end `81EDA0/176`, mode control and the two
synchronous call blocks. The dispatch `9B4560/58` also matches.

The following helpers **differ** on disk: `606F20/70`, `607010/73`,
`606B60/40`, `606B90/47` and `606B50/15`. The closed trial 16
`20261030T105708-19480` then corroborates the first four in official
memory, identical to the secondary in their complete bodies. The
destruction helper `606B50/15` is not included in this new measurement.
No protection mechanism was transformed or traced.

| Trial 16 range | Memory SHA-256, equal to the secondary |
| --- | --- |
| `606F20/70` | `5fd79ce5d8df893969f2fcd9b46b369c358935a5afcc488fbf16b490e509f574` |
| `607010/73` | `c7e8cb76a16eb78d2374cc891da6d588a800a90a3baba916fb32ea266be2a3f6` |
| `606B60/40` | `e5b0f56172623db11f3f322ce3f88c2ce582010d3ddefd88b17056c150c9e6a9` |
| `606B90/47` | `8cab123d9a681b6f56c4383851f4e84cafd30bbda9429d771a53b1cb0b1bb4cb` |

The private report is
`<isolated-copies>/slot0/20261030T105708-19480/code-helpers/comparisons.json`:
47 ranges, 12,146 bytes in total. The API names are corroborated by the
code and by the secondary's imports export; the official IAT in memory is
not reread by this investigation. Code equality does not prove its execution.

The earlier official corroborations cover the complete callback, the
worker and the integrator prefix; trial 14 adds its epilogue
`81EDA0/176`. These code equalities do not demonstrate its execution. In
the menu of the closed trial `20261030T102129-30308`, the 26 samples give
mode 1, a measured delta source, float period `0.0111111114`, `EB=EC=0`,
`ED=1`, last callback MS zero and integrator entry counter zero. No world
or real physics cadence is validated.

## ABI to respect

| Routine | Contract established by the instructions |
| --- | --- |
| `81EE50` | x86 WinMM callback with five 32-bit arguments; `RET 14h` at `81F0A1` |
| `81DFD0` | Three arguments **on the stack**, object at `[EBP+8]`, float delta at `[EBP+0C]`, second float at `[EBP+10]`; `RET 0Ch` at `81EE37` and `81EE42` |

A compatible call type for the integrator is
`void (__stdcall *)(void* service, float delta, float argument2)`.
It is not a thiscall: `ECX` does not carry `this`.
At `81DFDE`, the integrator loads its object into ESI from the stack.

The callback pushes zero, delta, then `1293880` before the call `81EFA9`.
Its second float is always **+0.0**. The two synchronous calls also push
the object explicitly, but their second float comes from the time supplied
to the service. The names "total time" or "phase" for this field are not
needed to establish the ABI.

The exact WinMM prototype appears in the local SDK
`10.0.26100.0/um/mmiscapi2.h`: `UINT uTimerID`, `UINT uMsg`, then three
`DWORD_PTR`. On the x86 process, they occupy five 32-bit words.
A hook DLL for the client must therefore be x86; the standalone C++ x64
model does not by itself provide this calling compatibility.

## Nominal chronology allowing an early installation

| Step | Exact sites and effect |
| --- | --- |
| Program entry | `9B4FE3` calls `97CDE0`; `97CDF2` calls the root virtual-init |
| Core services | `9B4E40` registers them successively through `9B4900` |
| Renderer, sixth service | `9B4EA7` loads `ECX=1140A00`, `9B4EAC` calls `9B4900` |
| Call of its initialization | `9B4939` calls `service.virtual+8`; `F07F94+8 = 996850` |
| Graphics creation | `996AE1` calls `50A030`, before `renderer+24=1` at `996B71` and its return |
| Rest of the 13 services | The root-derived init `F0B138+4 = 97C8F0` is only called at `9B4F2B`, after the 13 successful initializations |
| First loading scenes | `97C8F0` can call `97F260` at `97C9DA` and `97CA5F` |
| Addition of physics | `97CBBA` loads `ECX=1293880`, `97CBBF` calls `9B4900`, which invokes `84D570` through `F27068+8` |
| Worker creation | `84D990` pushes `81F0B0`, `84D997` calls the thread creator |
| Main loop scenes | They are built by `97CDE0` after the return of its virtual-init at `97CDF2` |

The return addresses **`996AE6` and `9B4EB1`** correspond to the graphics
creation nested in this precise initialization. Their admission on the
active stack of the same thread is more precise than the renderer byte `+24` alone.
The intermediate return of the service virtual-init is `9B493C`.
The complete 815-byte renderer body is already corroborated in official
memory; see [retail_video_defaults.md](retail_video_defaults.md).

The eight observed references to `97F260` are the two in `97C8F0`, then
`97CF6B`, `97D2A7`, `97D2EA`, `97D3D9`, `97D431`, `97D55A` in `97CDE0`.
The seven derived world constructors each have one code reference,
also after this initialization:

| Constructor | Site in `97CDE0` |
| --- | --- |
| `726EF0` | `97D145` |
| `73EB10` | `97D05E` |
| `76D780` | `97CF5D` |
| `78EC00` | `97D0CE` |
| `793880` | `97CEEF` |
| `7B3720` | `97D367` |
| `7B4880` | `97D10D` |

This establishes a **nominal phase prior to the scenes and the physics
worker** for a synchronous gate in this first graphics creation.
The finding does not prove the absence of all threads in the process:
generic workers may already exist, and indirect calls or external DLLs
are not exhaustively resolved. A transaction that inspects threads is not
by itself a universal creation barrier.
The installation must remain limited to the right identity, the right
architecture and this verified startup path; no late admission is inferred
from a mere `+24=0` flag or from a value resembling an address on the stack.
The creation helper stores its handle at `606F5A`, after the
`CreateThread` call at `606F54`. The arguments of `84D997` give zero
creation flags. A still-null `service+87A0` handle therefore does not
constitute a startup barrier. In trial 16, the current collection of five
frames does not establish the stack admission required for the two
creations; no background patch was applied there.

## Owners of the integration calls

The targeted scan of direct calls, the disassembly and the Rea references
establish three sites in the image:

| Site | Owner and condition |
| --- | --- |
| `81EFA9` | WinMM callback, according to QPC and `ED/EB/EC` |
| `84DF4D` | `84DE60`, mode `10E765C == 0`, sub-step loop capped at five |
| `84E16F` | Same mode-zero branch, one call with the supplied delta when the sub-step quotient is zero |

At `84DEF3..84DEFA`, a non-zero mode skips both synchronous paths.
In **stable mode 1**, they therefore do not duplicate the callback. The
worker creation is also conditioned by a non-zero mode (`84D96F`) and by an
absent handle (`84D967`). Mode 2 uses the same callback with another
internal priority argument.

The observed direct writes of the mode are `987926=1`, `98795B=2`,
`987977=0`, in the `PHYSICSTHREAD` configuration callback. The immutability
of the setting after startup is not proven. A concurrent mode 1 → 0 switch
can open the synchronous path while a callback is in flight;
rereading the mode without a handover protocol does not constitute a drain.
The observed references do not prove the absence of any indirect call
or of code added by another module.

## Flags: producers and consumers

The service base is `1293880`. The raw names avoid confusing
pause, transition, zero delta and service lifetime.

| Field | Established producers | Consumption in `81EE50` |
| --- | --- | --- |
| `129BEEB`, `+866B` | `7B3486=1`, `7B39B3=0`, branch `7B41B4=0`, `8E587F=CL`, `8E808A=1` | If non-zero and ED zero, call with zero delta |
| `129BEEC`, `+866C` | Init `84D58F=1`; `84DE85=0` then `84DE8E=1` if the service delta is zero; teardown `846D00=1`, `8471B2=0` | Same zero-delta call |
| `129BEED`, `+866D` | Constructor `847441=1`; init `84D596=1`, normal end `84DE39=0`; teardown `846D07=1`; program exit `97D617=1` | If non-zero, no integrator call |

`84DE39` belongs to the end of the init `84D570`, not to a standalone setter.
EBX is reset to zero before this end; the normal write of BL there is zero.
The paths `7B3450` and `7B4120` involve transitions/cameras and
scene tracking. The gameplay context of the two `8E...` writes remains untyped.
The offset-based search is not a complete analysis of aliases or of
grouped writes; the callbacks also read the absolute addresses.

The counter `16A69F0` is incremented **before** entering the critical
section of the integrator. The counter `10E7A50/54` describes admissible
callbacks, even when ED suppresses the integration. None of these counters
proves by itself a completed positive integration.

## Observed shutdown and absence of drain evidence

`F27068+1C` contains `846CF0`. The generic teardown `9B4560` walks the
services in reverse order and calls this slot at `9B4585`; `97BF20` calls
this teardown. The physics slot `+0C=841850` is only `RET`.
`+14=84B650` handles physics drawing/debug. Confusing them with the shutdown
would give a wrong hook point.

The routine `846CF0` follows, among other steps, this order:

1. Writes EC and ED to one, then removes references and tears down arrays
   and objects of the service. The world is not kept intact by this routine.
2. If the mode is non-zero, writes `service+8670 = 1` at `847102`.
3. Spin-waits for this field to equal two, at `847111..847117`.
4. Removes the thread object `service+8798` from the registry, calls `607010`,
   then continues tearing down the lock and the other resources.

The init resets `+8670` to zero at `84D59D`. The worker reads this field at its
absolute address `129BEF0` after `Sleep(500)` calls. When the value is one, it calls `timeKillEvent` at
`81F181`, writes two at `81F183`, then **loops back to Sleep** at `81F170`.
There is no visible cooperative exit in these 223 bytes.
The result of `timeKillEvent` is not checked before the acknowledgement.

The body `607010/73`, corroborated in official memory by trial 16,
performs the calls identified as `GetExitCodeThread`, then, if that
call succeeds, requests `TerminateThread(..., 0)` and `CloseHandle`, and finally
clears the handle and the identifier. No `WaitForSingleObject` appears there.
This establishes forced termination code, not a cooperative join.
Its execution during a physics shutdown has not been observed.
The real worker handle is at
`service+87A0`, in the object `+8798`. `10E749C` is an object reference
with 16-bit counter management, not this thread handle.

The local SDK defines `TIME_PERIODIC=1` and `TIME_KILL_SYNCHRONOUS=0x100`.
The worker passes only one to `timeSetEvent`. The acknowledgement value two therefore
does not provide the prevented-callback guarantee described for the
synchronous flag, nor proof that the current callback has exited. The native
boolean `10E7C08` is tested/written without an interlocked primitive.
Observing zero once does not prevent a future callback from entering.

The bodies `606B60/40` and `606B90/47`, also corroborated in official
memory by trial 16, wrap a Windows critical
section at `service+87D0+4`, with ancillary statistics. The integrator
calls it at `81E003` and releases it at `81ED81`, before its final profiling.
Even an enter/leave pair
of the lock would not prove a drain of the timer: a callback can be before
the lock or waiting to enter it. Mutual exclusion does not remove
successive duplicate steps.

## Proposed C++ callback contract, before implementation

- Keep a single WinMM producer and replace the whole callback; do not
  call both its historical trampoline and a new integrator.
  Create no second 90 Hz timer. Require the planned mode 1 and handle
  any mode transition explicitly.
- Initialize a monotonic clock owned by the scheduler. Accumulate QPC
  time, produce `1/90` steps bounded in number, keep the remainder according
  to the new policy. An unchanged QPC value adds no time. Do not repeat the
  dispatch `97E860`, nor the scene timer `982AC0`.
- Preserve the priority ED > EB/EC. ED suppresses the call; EB/EC produce
  a zero-delta call in the historical contract. All three can
  interrupt progress, but they are not equivalent gameplay reasons.
  A pause must not accumulate a debt to replay on resume.
- Revalidate the lifetime states between catch-up sub-steps. The teardown
  notification can begin during a long callback; do not keep
  only a copy of the flags taken before the whole loop.
- Plan a timer/service generation and a long-silence policy.
  A shutdown/reinit may deliver no callback while ED=1. A changed
  `uTimerID` is a useful hint, but its possible reuse does not constitute
  a guaranteed generation. An explicit lifecycle event
  provides a stronger guarantee. Service shutdown at each world
  change is not established by this investigation.
- Let the host timer own `FA3FC0/FA3FC8`, their scene resets
  and the replay. The callback uses a local scheduler; it forces
  neither these globals nor the focus. `81DFD0` already applies its clamp and the
  factor `F8A2EC`: do not apply the factor a second time to a delta assumed to be already scaled.
- Keep distinct the number of notifications, the number of sub-steps,
  zero-delta calls and completed positive steps. If the native diagnostics
  are reused, preserve their definition instead of giving them a new meaning.
- To detach/unload, close the producer, establish a guarantee on
  its future callbacks, then wait for the active calls. The observed native
  protocol does not prove these three conditions. A hook kept until the end
  of the process avoids an arbitrary detachment, without by itself validating
  the teardown of the world's resources.

## Remaining verifications

The four helper ranges transmitted then read in official trial 16
are `606F20/70`, `607010/73`, `606B60/40`, `606B90/47`, that is
**230 bytes**. Their exclusive ends are respectively `606F66`,
`607059`, `606B88`, `606BBF`. Their corroboration closes the unknown on the
wrapper bodies, without validating the execution of a shutdown/drain or the
stack admission of the future substitution.

Before any physics trial: x86 fixture of the two ABIs with stack
sentinels, startup path admission/failure, concurrent notifications,
pause/resume without debt, shutdown/reinit and mode change, reused timer,
bounded delay, refusal of a double owner. The standalone historical model
already has 2,002 checks; it is not a validation of the new
scheduler nor of its integration into the game.

A measurement in an actually loaded world must then distinguish wake-ups,
entered calls, lock waits, positive delta and integration end.
The profile-less menu and the zero counter of the previous trials do not
replace this validation. This investigation launched no game, performed no
UI click, and changed no code or CMake. The new runtime evidence
comes from the already justified isolated trial, conducted separately.
