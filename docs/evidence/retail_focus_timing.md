# Focus, clocks and physics scheduler of the PC client

## Conclusion and scope

The Windows message pump `0x00433260` does not wait for focus. The program has
several other execution gates, input activation handlers and a game-logic
branch that can be triggered when the application becomes inactive.
Artificially holding the global activation flag at one would therefore be too
broad a change. A C++ "continue in background" policy must remain distinct
from the real activation and from the pause requested by the player.

The game timer and the physics callback have distinct paths. Neither a
successful present, nor an advancing clock, nor an executed timer callback
proves on its own that a positive physics step has completed. This document
establishes measurement and reconstruction points; it delivers no injected
fix. No game or secondary binary was launched for this investigation.

## Identity and evidence

All addresses are x86 VAs, image base `0x00400000`.

| Image | SHA-256 | Role |
| --- | --- | --- |
| PC MC 1.66 A, `<installation>/TestDriveUnlimited.exe` | `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30` | Primary reference |
| Executable associated with the local Platinum/Paradise patch | `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed` | Secondary source, never labeled "official" |

Local evidence under `<private-evidence>/reprise-2026-10-30/rea/`:

- `original-display-timing/35-procedure_assembly.json` and
  `36-procedure_pseudo_code.json`: loop `0x0097f260` of the original.
- `paradise-focus-timing/`: pump, window procedure, activation, timer,
  dispatch, physics options; bounded `*.asm.txt` listings as a complement.
- `paradise-focus-follow/`: owners of the other activation readers, timer
  structure, physics callback and PE imports. The folder contains
  `targeted-disk-comparisons.json`, the Rea outputs, the bundle and the
  snapshot.

Rea uses Ghidra 12.2. Some decoded ranges are not recognized as functions:
notably `982ac0`, `81ee50`, `81f0b0`, `433420` and `98ef90`. The decompilation
refusals are preserved. Their reading relies on
`inspect_native_instruction`/`read_bytes` and on a **static** MSVC complement
`dumpbin /DISASM:NOBYTES /RANGE:...`, version 14.44.35229.0. This complement
creates no Ghidra function and does not execute the image. Both secondary
sessions are exported and closed; the non-zero exit code preserves the
expected refusals of some requests without invalidating the successful evidence.

The official memory comparisons have a precise scope:

| Closed trial | Range read | Result |
| --- | --- | --- |
| `20261030T074026-34732`, 17 ranges, 3 648 bytes in total | `433260/512` | Differs from the official disk, equals the secondary; SHA `b23831f75f2cfd5036d9455c719a39baea04bb58d78d68d434be3cb9d5229ffa` |
| Same trial | `982ac0/512` | Equals both disks; SHA `8c5128f6b84d24f5255ac5e47da0095bb16892f2baaf50cabc27fcfcb85f4ac4` |
| Same trial | `97e860/512` | Equals both disks; SHA `2a03970ab5ee12ba537da74b4291b9a1bc6926281ec0ff22479c171583a8600e` |
| Same trial | `9878e0/320` | Equals both disks; SHA `62b3b15c949722d1de6932e7f4dee0f1c7c94bea4a740ed61146d372e2259237` |
| `20261030T081533-16432`, 19 ranges, 3 895 bytes in total | `433648/140` | Differs from the official disk, equals the secondary; SHA `83b3ef5f88e8d9c443b1320e2346a419543fcd0a6f0a25bfe518a756bb80f762` |
| Same trial | `98ef90/107` | Equals both disks; SHA `8b522aac430471b95c9fac0a885218c6f948d52b4158e2968970a1b494fa6c16` |

The original reports are in `<isolated-copies>/slot0/<run>/code-helpers/comparisons.json`.
Equality of a prefix does not extend to neighboring functions, pointed data
or other readers of the same flag.

## Pump, keyboard focus, activation and shutdown

`433260..4332b7` forms a complete pump of **88 bytes**. It drains the
available messages with `PeekMessageA(..., PM_REMOVE)`, then calls
`TranslateMessage` and `DispatchMessageA`. It returns when the queue is
empty. No `Sleep`, `WaitMessage` or focus test appears in it. The 512-byte
memory prefix covers this complete body, then other routines; `4332c0` in
particular is a language selector.

The secondary window procedure starts at `433420`. An optional filter
`16990dc` is consulted before its own handling. The following notifications
are distinct:

| Message | Attested state and effect |
| --- | --- |
| `WM_SETFOCUS` / `WM_KILLFOCUS` | Byte `16990c0` and callback `16990c8(bool)` |
| `WM_SIZE` | Minimized state `16990c1`, maximized state `16990c2`, callbacks `16990cc`/`16990d0`, then size callback `16990d8` depending on the changes |
| `WM_ACTIVATEAPP` | Boolean derived from `wParam == 1`, written to `16990c3` at `4336b6`, callback `16990d4(bool)` on change |
| `WM_CLOSE` | Callback `16990c4`, bound to `97e560` by `97f260` |
| `WM_DESTROY` | `PostQuitMessage(0)` |

The callback `97e560` contains only eight bytes: it sets `10e72f7` to one and
returns. It is a shutdown signal, **not** the focus-loss pause.

`98f0aa` binds the activation callback to `98ef90`. The latter, whose
**complete 107 bytes** are corroborated by trial 9, calls a device/display
restoration path when the conditions require it, then obtains an interface
through `433df0(&FA9D30, 1, 0)` and calls its virtual slot `+8` when active,
`+4` when inactive. The activation role of the interfaces is clear; the exact
contract of each implementation remains to be typed. The body directly
modifies neither the time factor nor a manual pause.

## Why `16990c3` must not become a global policy

The secondary references observed by Rea include the following uses. This
list of static references does not guarantee the absence of indirect aliases.

| Reader | Effect to preserve or to examine separately |
| --- | --- |
| `97f2c1`, `97f661` in `97f260` | Scene entry/exit gates: while inactive, pump `433260` and service `5066e0(0)`; the scene does not advance during these waits |
| `6c6041` in `6c6030`; `6c7091` in `6c7010`; `74dcb7` in `74dca0` | Similar waits before configuration/device operations; these are not simulation steps |
| `980700` | If inactive, calls `433dc0(&FA9D30)`; this routine walks the interfaces and calls their virtual slot `+4` |
| `7bec2c` in `7beb90` | Inactivity takes part in a game-logic condition involving several other flags; the branch modifies bits and calls virtual slot `+0x30` with `1` |
| `87db47` in `87dab0` | After several services, if inactive calls `87bad0` and writes `object+BD54=1`, otherwise writes zero; this path has other effects through the online services |
| `8b02c7` in `8b0230` | If inactive, skips a large block of computations that use matrices among other things; the exact camera/render semantics remain to be determined |

`433dc0` **does not directly clear an array**: it invokes `virtual+4` on the
objects of the list (`base+8c`, count `base+84`). The term "clear" would be
more precise than the evidence allows. Keeping `C3=1` would nevertheless skip
this handling, even if `false` were correctly passed to `98ef90`.

In the main body of `97f260`, the calls to `97e860` do not all carry a local
test of `C3`. One can therefore conclude neither "everything freezes in the
pump" nor "the whole simulation continues". The `7beb90` branch must be
linked to its real virtual target before it is named an automatic pause or
has a condition removed. The manual pause is not established by this reader
alone.

For a C++ reconstruction, keep the real activation for inputs, device
acquisition and Windows messages. Introduce an independent execution policy
only at the scene waits, once their effects are understood. Modify neither
`GetForegroundWindow`, nor the messages, nor the shutdown signal. Keep the
graphics configuration waits until their D3D12 replacement is validated.

## Timer and progression scalars

`982ac0..982bfd` is a complete body of **318 bytes**, entirely contained in
the official 512-byte read. It does not test focus.

The global object `FA3F90` carries the vptr `F03534`; its slot `+0x10`, at
`F03544`, holds `982ac0`. It is registered at `9b4e83` through `9b4900`. The
dispatch `9b4830` calls this slot on the selected services. Rea
`paradise-focus-follow/75..78` establishes these links. The initial object and
the 32 bytes of vtable are identical between the two disks.

On the normal path, the timer obtains QPC (fallback `timeGetTime`), subtracts
the base `1699110/14`, divides by the frequency `1699108`, multiplies by 1 000
and keeps the low 32 bits of the milliseconds. It converts the unsigned value
to float, subtracts the previous time, then multiplies by approximately
`0.001`.
The delta is capped at one second; no explicit floor appears.
On the fixed path, bit zero of `1140393`, the delta comes from
`uint8(1140389) * float(114038c)`; this path does not go through that cap.

| VA | Explicit read | Meaning established by the timer instructions |
| --- | --- | --- |
| `016a2c18` | `uint32`, 4 bytes | Current milliseconds, modulo `2^32`; synthetic in fixed mode |
| `00fa3fac` | `float32`, 4 bytes | Total of raw deltas, seconds (`object+1c`) |
| `00fa3fb0` | `float32`, 4 bytes | Last absolute millisecond value (`+20`) |
| `00fa3fb4` | `float32`, 4 bytes | Published raw delta (`+24`) |
| `00fa3fb8` | `float32`, 4 bytes | Saved factor (`+28`) |
| `00fa3fbc` | `float32`, 4 bytes | Current factor (`+2c`) |
| `00fa3fc0` | `float32`, 4 bytes | Scaled total, seconds (`+30`) |
| `00fa3fc8` | `float32`, 4 bytes | Scaled delta, seconds (`+38`) |
| `00fa3fcc` | `float32`, 4 bytes | Current raw delta (`+3c`) |
| `016990c3` | `uint8`, 1 byte | Activation actually received by the window procedure |

`FA3FC4` (`+34`) remains excluded from the proposal: its meaning is not
established. These fields are time and control scalars; no read of a profile,
an account or user input is needed.

The small helper `982a30` sets the current factor to zero or restores the
saved factor depending on its boolean. Its role is compatible with a timer
pause, but the "manual menu", "scenario" or "activation loss" origin must
not be attributed to all its callers without evidence. The factors are also
modified by other systems. Forcing them to one would cancel game behaviors.
The totals are reset at scene transitions; they are not global monotonic
clocks. The float representation of the milliseconds and the integer
wraparound must also be taken into account by telemetry.

`97e860`, a complete body of **299 bytes**, handles the record/playback
modes before distributing the updates through `9b4ac0`. In playback, it
explicitly replaces `FA3FC8` with a recorded value and reloads an input data
block. Its calls must therefore not all be treated as a simple context-free
tick, and this routine must not be called a second time to "catch up" on
time.

## `PHYSICSTHREAD`, substeps and integration

The option callback `9878e0` recognizes `OFF`, `NORMAL` and `REALTIME`. It
writes a mode to `10e765c` and a frequency integer to `F87FFC`, depending on
its branches; application state 13 also forces mode zero. `F87FFC` is 100 in
the initial image, **not necessarily in the current process**. Mode two is
distinguished by an internal priority argument; this does not prove a
"real-time" Windows priority.

The path `84d930..84dadf` creates, when the mode is non-zero and the thread
absent, a worker thread whose entry point is `81f0b0`. Its static body of
**223 bytes** is identical on both disks. It computes `1/frequency`, obtains
QPF and the WinMM capabilities, then calls `timeSetEvent` with the integer
delay `1000/frequency`, callback `81ee50`, user value zero and flags `1`
(`TIME_PERIODIC`). The worker itself performs `Sleep(500)` and destroys this
timer when a stop state requests it.

The secondary imports are attested by Rea `89-inspect_pe_imports.json`:
IAT `9ec4a8=timeSetEvent`, `9ec4ac=timeGetDevCaps`, `9ec4b0=timeGetTime`,
`9ec4b4=timeKillEvent`, `9ec0f0=QPC`, `9ec0f4=QPF`, `9ec158=Sleep`.
The flags/prototype correspondence is verified in the local Microsoft SDK
10.0.26100.0, `um/mmiscapi2.h` lines 29 to 65. The IAT of the official disk
has a different layout; do not transfer these locations to it automatically.

The callback `81ee50..81f0a3`, **596 bytes**, is also identical between the
two disks. Trial 10 then corroborates its body in official memory, in two
contiguous reads of 512 and 84 bytes.
It has a reentrancy guard and compares the elapsed QPC time to half of the
configured step. If it passes this threshold:

- `129beed != 0` skips the integration call;
- otherwise a non-zero `129beeb` or `129beec` causes `81dfd0` to be called
  with a zero delta;
- otherwise `F8A2E8` chooses between the measured delta and the configured step;
- it then increments the 64-bit counter `10e7a50/54`, even if the first
  flag suppressed the integration.

The game-logic meaning of the three flags remains uninterpreted. It is not
established that they all designate the manual pause. The counter counts the
callbacks that passed the threshold, not positive physics steps.

In mode zero, a branch of `84de60` computes `h=1/F87FFC`, then truncates
`delta/h`. If the result is zero, it integrates once with the supplied delta;
if it is positive, it caps it at five and calls `81dfd0` with `h` that many
times, advancing the total time argument. No remainder accumulator is
visible in this branch. The Ghidra function is non-contiguous: this finding
proves neither that all call contexts follow this branch, nor a general link
between display FPS and simulation speed.

At the start of `81dfd0`, a delta strictly greater than about `2/90` second
is replaced by about `1/90`, then multiplied by `float(F8A2EC)`. Other modes
may still subdivide it. This threshold does not prove that the game always
uses a fixed 90 Hz frequency. The 112-byte prefix containing this computation
is identical between the disks and to the official process in trial 10.
The complete routine touches many world/vehicle systems under lock; it is not
an isolated reentrant mathematical function.

The integer `16a69f0` is incremented on entry to `81dfd0`, at
`81dfee..81dffa`, before its lock, then used for duration statistics. The four
references observed by Rea `91-xrefs.json` show no other writer.
A `uint32/4` read allows counting the entries between two samples, modulo
`2^32`, without profile data. An entry may nevertheless have a zero delta or
wait on the lock: it does not prove a completed positive integration.

To decouple rendering in a reconstruction, choose a single owner of the
physics steps, with synchronization and transfer of the results to the
renderer. Adding a fixed loop while the WinMM timer stays active risks
doubling the calls. Explicitly preserve the time factors, the replay modes,
the substep bounds and the manual pause before modernizing them.

## Minimal validation to prepare

During a future trial that is already justified, a bounded probe can record
`C3/1`, `16A2C18/4`, `FA3FAC/4`, `FA3FBC/4`, `FA3FC0/4`, `FA3FC8/4`
and, with its explicit static provenance, `16A69F0/4`. Read only these
authorized addresses, never a wide neighborhood. Timestamp each sample with
a clock of the probe; do not present successive reads as an atomic snapshot
of the game.

Compare the sequences active → other window → active, then manual pause
with and without focus. Observe the deltas between samples and the scene
resets. A zero factor with a growing raw total and a fixed scaled total
identifies a suspension of this clock; it does not identify its cause by
itself. An advancing integrator counter proves entries, not the final state
of a vehicle. Measure the presentation rate separately and read no pixel or
profile for this finding.

Before any change to execution without focus, the `virtual+0x30` target of
`7beb90`, the origins of the physics controls and the effects of the inactive
online path remain to be typed. The next fix can then target a precise
execution gate while preserving the real inputs, the manual pause and
shutdown.

## Initial measurement and corroboration from trial 10

The C++ [bounded telemetry](runtime_timing_probe.md) probe records the scalars
established above during trial `20261030T083003-25076`, PID 9424. The 26
samples over 5.287331 seconds read their 53 bytes without failure: the raw
and scaled totals advance, the factor is one, the physics frequency is 90 and
the integer mode is one. The integrator counter stays at zero in this menu
without a world. The PID is never in the foreground, yet C3 stays at one; no
received activation-loss notification is proven. No focus or physics-step fix
was applied during this measurement.

The 27 code ranges (6 518 bytes) include the integrator `81dfd0/112`, the
complete callback `81ee50/512 + 81f050/84` and the worker `81f0b0/223`: the
four ranges are identical to both disks. The callback body is therefore
corroborated in official memory, without demonstrating its execution in this
scene. The loading duration and the successive reads do not make it an
atomic snapshot. The private comparisons are kept with the trial.

This new trial is closed, with the slot released and the sources unchanged.
The remaining steps are the real focus transitions and driving in a loaded
world, then the separation of the owners of ticks and rendering.
