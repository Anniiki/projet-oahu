# Continuity without activation: official client bridge

## Scope

The C++latest Win32 component [retail_background.cpp](../../mod/src/retail_background.cpp)
adapts three branches of the official client **in memory only**. It is a
temporary compatibility bridge; the world is not fully recompiled.
The real activation `16990C3`, its writers, input cleanup and the manual
pause callbacks remain intact. No retail file, account, save or protection
mechanism is modified.

The primary reference is the PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The Rea evidence and code comparisons are in
[retail_simulation_dispatch.md](retail_simulation_dispatch.md) and
[retail_focus_timing.md](retail_focus_timing.md). Trial 14 corroborates the
complete original bodies of `7BEB90` and `97F260` before any adaptation.

## Branches and effects

| Site | Modification | Bounded effect |
| --- | --- | --- |
| `7BEC34` | Prefix `0F85` → `90E9`, rel32 unchanged `C3` | After no action request, join `7BECFD` regardless of focus |
| `97F2C8` | `74E6` → `9090` | Windows pump and service run once, then scene entry without waiting for activation |
| `97F668` | `74E6` → `9090` | Same policy at scene exit |

The manual request `7BEC2A→7BEC3A` keeps its `750E` bytes and therefore still
jumps directly to the pause checks. The other game-logic guards, the virtual
scene recipients, registers, stack and flags are not rewritten. The final C++
policy will explicitly separate the manual pause reason from
`!active && !continue_in_background`.

## Admission and lifecycle

The private option `TDUR_RETAIL_BACKGROUND=1` comes from the D3D12 trial
wrapper; `--keep-focus-pause` keeps the baseline. No global environment is
changed. The launcher opens the executable without write/delete sharing,
checks its SHA and keeps that handle until the process ends. It overwrites the
inherited marker `TDUR_RETAIL_EXECUTABLE_VERIFIED`, even for a fixture. The
module arms itself only during initial loading (`reserved != nullptr`); a late
`LoadLibrary` is refused before any hook. Identity is rechecked outside
`DllMain`, before the three gameplay adaptations.
Another image is refused without any write. The base must be `00400000`,
the sites must belong to its executable image and the 26-byte signatures
must match exactly.

The [renderer gate](../../mod/src/retail_background_gate.cpp) is armed in
the same pre-entry transaction as the public D3D9 factory. Its signatures
include the prologue, `RET4` and known call sites; name, x86 PE image, base
and launcher marker are required. It is a pass-through redirection before the
second SHA check, not graphics work executed in `DllMain`. After commit, the
entry and trampoline protections, both jumps, the relocated instructions and
a controlled flush are checked explicitly; Detours success alone is not
sufficient.

The C++ entry adapts ECX/self, a pointer passed on the stack and the AL
return. Only the first global invocation, `self=1140A00`, direct return
`9B493C`, can open the scope after verified identity. The ordinal is consumed
before any refusal.
The `CreateDevice` callback requires native success, TLS depth exactly one,
a consistent initial parent, the HWND owner thread and `renderer+24=0`.
Nested calls and preliminary creations cannot borrow the scope of another
call. It is closed by `__finally` on exit from the initializer, including
after an exception; Reset does not open it. A probe presentation therefore
does not prematurely seal another device.

In the audited path, `9B4E40` initializes the renderer among the 13 core
services. Only then does `9B4F2B` call the derived initialization `97C8F0`,
its first scenes `97F260`, then the physics initialization. This time bound
is essential: a thread inventory alone is not a barrier against the creation
of new threads.

## Transaction and errors

[code_patch.cpp](../../mod/src/code_patch.cpp) prepares the handles and every
allocation before suspending the other threads of its own process. Each
handle is rechecked by PID. An EIP inside a targeted instruction refuses the
edit. Under suspension: signatures reread, pages temporarily writable,
aligned CAS16, check of the resulting signatures and instruction cache,
then protection restoration and thread resumption. The two scene sites
share one page, restored only once.

An error undoes only the words written by that transaction. If the state or
its protections cannot be fully verified, the component terminates **its own
private process before any resumption**; it does not continue with a partial
patch. A persistent error refuses subsequent creations. The suspension is
not a general engine synchronization: keep this initial point bounded.
Microsoft describes the risk of waiting on a lock held by a suspended thread
in its
[SuspendThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-suspendthread)
documentation. The reconstructed engine must use explicit cooperation of its
workers.

## Validation

The source-owned CPU tests pass under MSVC 19.44 Win32, `/std:c++latest
/W4 /WX`. The first runs small fixture functions: the four active/inactive
combinations and manual pause remain correct; both pumps continue without
fabricating activation. It also checks a wrong signature, alignment,
duplicate, canaries, RX rights, resumption of a real thread, busy EIP and
rollback after open, flush or restore errors. The second forces an impossible
restoration and checks that the child fixture stops with `ERROR_WRITE_FAULT`,
without resumption or game file. Together they total 41 transaction checks.
An ABI fixture uses a real compiled virtual call: ECX/parent/AL, reentrancy
and SEH exception traverse the real wrapper, with dummy Enter/Leave. A second
part actually places Detours on a source-owned stub at the same prologue,
validates the trampoline and its rights/flush, traverses it then detaches it,
recovering bytes and protections. Injected fixtures refuse their own identity
without applying the bridge. The launcher checks verify that the inherited
marker 1 becomes 0, and its independent SHA; a separate test refuses late loading
without creating a log.

Official trial 15, `20261030T105411-29596`, PID 31088: stack admission
refused at the first device, before any write. The game returns 1 after
21.3877 s; the audits confirm the 6 697 files and 17 source saves unchanged.
This refusal demonstrates neither the first-device phase nor background
driving. The next iteration distinguishes preliminary creations and logs only the
indices of the expected returns, not a full stack.

Trial 16, `20261030T105708-19480`, PID 22412, 75 s: both stack walks return
five frames without the expected returns, with owner 1 and known CALL
conforming. No gameplay adaptation is applied; closure `not-applied`.
Rendering stays at 2560×1440 borderless, AA/HDRI/LOD2/1/2. The 47
ranges/12 146 bytes and 28 scalars/91 bytes x26 are readable; source audits
unchanged. The native bodies reuse EBP, which limits standard unwinding. This
path was replaced by the explicit entry above, without raw stack exploration
or relaxation of the admission.

## Attested official installation

Trial 17 closed, `20261030T112335-28184`, PID 26820, 75 s: the launcher
report confirms the official SHA, `executable_locked=true` and
`retail_executable_verified=true`. The log attests the pre-entry gate armed,
the first native init admitted, the adaptations applied, then closure at init
exit. Two initial applications of the AA/HDRI/LOD2/1/2 preferences and the
DXGI S_OK presentation follow: source/viewport/output 2560×1440, borderless,
160/1 Hz, configured ceiling 160 Hz, presenter focus 0.

The probe reads 47/47 ranges and 12 146 bytes. In the four touched ranges,
the **only deviations** from the reference bodies are the three 2-byte
prefixes and the seven bytes of the detour at 996850. The remaining
instructions and branch displacements match entirely. The comparisons are in
the `code-helpers/comparisons.json` file produced with this trial; they
explicitly identify the modifications made by our component, without
attributing them to the original game.

The 26 graphics samples read 28 values/91 bytes without failure: native
activation 1, factor 1, period 1/90, measured delta source 1, EB/EC 0, ED 1
and integrator counter 0. The menu advances; no loss of the world's real
activation is observed. The job object closed its only copy, slot released, 6 697 files
and 17 source saves unchanged. No online profile was created.

DLL: `e136c22bbf6f765a89a76d35eaad3084ce98b7f6b0c3f3b825d8564e039b0038`.
Launcher: `cdf11e16200ea62124073820bcd7c72d3ed5322dee2204a8bf95a3409c8bd479`.
Log, 15 323 bytes:
`a918fc8ffa1c7dafe7d168e16f5f3c38d710853c1457e387940632fa565000ca`.
Report, 2 579 bytes:
`ad74f92010e93195b9d107e40e9af759378f7335963a13c0267e974e1cb70a61`.
The CPU checks and the seven targeted validations of the latest version
pass, including 12 ABI/trampoline checks. The mod now has 20 CTests.

Still to attest in a loaded world: a real activation change, continuous
driving and NPCs, manual pause, out-of-focus scene transition and an
unavailable device. No test of this bridge validates multiplayer.
