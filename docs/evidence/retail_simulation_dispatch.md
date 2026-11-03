# Simulation dispatch, services and the rate boundary

## Usable result

`97E860` and `9B4AC0` combine simulation work, the Windows message pump,
the timer, inputs, online services and render preparation. Calling them
several times to catch up on 60 Hz logic would repeat those effects. The
reconstruction must assign a rate to the services and callbacks concerned,
then preserve their dependencies and barriers.

Physics already has a WinMM owner when `PHYSICSTHREAD` is active. Calling
its integrator in addition from a new 90 Hz scheduler would potentially
double the steps. The separation points are identified below; no
scheduling change is applied.

## Identity and scope of the evidence

Addresses are x86 VAs, base `00400000`.

| Image | SHA-256 | Status |
| --- | --- | --- |
| PC MC 1.66 A, `<installation>/TestDriveUnlimited.exe` | `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30` | Primary reference |
| Local executable associated with Platinum/Paradise | `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed` | Secondary analysis image |

The new evidence lives in the private folder
`reprise-2026-10-30/rea/`:

- `paradise-simulation-dispatch/`: loops, vtables, dispatch, constructor,
  pause and physics writers; `targeted-disk-comparisons.json` gives the
  lengths and SHAs of each exact comparison.
- `paradise-simulation-follow/`: references to the pause and to the derived
  constructors, registration of the core services.
- `paradise-simulation-services/`: additional references to `9B4900`, AI
  service, program vtable, pause variants and bounded reads of the bodies
  that Ghidra does not attribute to a function.

Rea/Ghidra 12.2 reuses the existing secondary snapshots. The unattributed
bodies are completed with `inspect_native_instruction`, `read_bytes` and a
static MSVC `dumpbin` 14.44.35229.0 disassembly. Refusals are kept. In
particular, the Ghidra record of `7BE2E0` lists only four instructions
while its bounded disassembly covers 197 bytes: its graph summary does not
represent all of the visible behavior. A list of callers attributed to
functions may also omit code references that are decoded but not
attributed.

The following complete ranges are identical between the two disks:
`9B4AC0/222`, `9B4830/203`, `9B4900/88`, `9B4E40/259`,
`97C8F0/1212`, `97F260/1147`, `7BEB90/1761`, `7BE500/167`,
`7BE2E0/197`, `969E70/546`, `998710/152` and `98EF20/8`.
The three ranges of the non-contiguous body of `97CDE0` also match
(154 + 42 + 1918 bytes). The AI initialization `969900/425` **differs**:
its secondary interpretation is not transferred wholesale to the original.
The equality of these ranges is static, with no equality assertion over the
whole PE.

The official memory corroborations of the timer, of `97E860`, of the WinMM
callback and of the integrator are detailed in
[retail_focus_timing.md](retail_focus_timing.md). They do not automatically
cover the new bodies of this investigation. No game was launched for this
static investigation; no account data was read.

A later corroboration, from the closed private
official trial `20261030T102129-30308`: the 14 added ranges cover the
complete bodies of `9B4AC0`, `9B4830`, `97C6E0`, `97F260`, `7BEB90`,
`7BE2E0`, `81EDA0` and `969E70`. All of them match in memory with both
disks. The display catalog reaches 43 ranges and 11,916 bytes. The exact
reports remain under `code-helpers/` in that batch; this is not an
execution trace of the routines.
See [the scalar measurement](runtime_timing_probe.md).

## Program, scene and display iteration

`9B4F60` constructs the context, then replaces its base vtable with
`F0B138` before calling `97CDE0`. The derived vtable must therefore be
read: its slot `+4` holds `97C8F0` and its slot `+10` holds `97C8B0`, a
jump to `97C6E0`. This preprocessing consults a queue and may wake the
worker `143F670`, tied to autosave through `97C830`. Its 331 bytes match
between the disks; its virtual interfaces remain to be typed. It must not
be repeated as a simple mathematical world step.

`97CDE0` initializes the services, chooses a scene according to
`root+100`, calls `97F260`, destroys the scene and then starts over until
`root+109`. It is a scene transition machine; it is not the logic tick.
`97C8F0` may itself launch loading scenes before the game services are
added. It must not be assumed that all scenes are already equipped with
the same services.

The main body of `97F260` follows this order:

1. `97E860(scaled delta, scaled total)`.
2. If `scene+39` is zero: `5066E0(0)`; otherwise `97E7C0()`, conditional
   processing `8444C0(delta)`, then `5066E0(1)`.
3. Deferred work, wait for completion and swap of the buffer indices.
   Autosave and transition processing also surround this cycle.

The activation waits at scene entry and scene exit pump `433260` and call
`5066E0(0)`. The transition paths `97E990` and `97ED60` also use the
dispatch and the display processing. Replacing only a driving loop would
therefore leave other scenes to handle.

`5066E0` consults slot `+14` of the object pointed to by `FAD8E0`.
Depending on its result, it runs `50EA50`, callbacks and the slots `+18`,
`+20`, `+1C`, or signals state 4. This path is tied to the device and
display cycle; it must stay out of the simulation catch-up. The exact
names of all these slots are not established here.

## Actual order of `9B4AC0`

`97E860` first handles the recording and replay of inputs. In replay, it
may replace the global delta and the input block before `9B4AC0`. The
dispatch then performs:

| Order | Call or effect |
| --- | --- |
| 1 | `root.virtual+10()`; derived vtable `F0B138` |
| 2 | `9B4830(delta, total, -1)` on the registered services |
| 3 | Publication of the phase context, then `9B4830(..., root+180)` |
| 4 | `9B4650()`: end-of-phase barrier |
| 5 | `scene.virtual+10(delta, total)` |
| 6 | `97E630(scene, delta, total, root+180)` on its nodes |
| 7 | New barrier `9B4650()` |

`9B4830` walks the service array `root+4`, count `+D4`; it selects
`service+14 == group` and `service+18 == 0`, then calls
`service.virtual+10(delta, total)`. `9B4900` registers a service, initially
assigns it the group `-1`, calls its initialization `virtual+8` and
increments the count. An initialization or an option may still modify
fields: the current group was not read from memory.

`97E630` walks the list `scene+14`, filters the group at `node+8` and calls
`node.virtual+4(delta, total)`. This list and the main scene update are
not identified as pure logic: they also contain visual systems, states and
transitions.

`9B4650` waits for the completion flags of the phase contexts. It is not a
queue drain. `9B4710` consumes deferred work; `9B4BA0` drains it and waits
for the worker to finish under its synchronization. `97E7C0` calls the
`+14` slots of the scene and of its nodes around this processing. The
observed constructor uses a phase loop bound equal to one; the branches for
other indices do not prove their activation.

## Services to separate

`9B4E40` adds 13 core services. The nominal path of `97C8F0` adds 38, that
is 51 in total if the preceding initializations succeed. The Rea export
`paradise-simulation-services/06-xrefs.json` keeps the 51 references to
`9B4900`, including those from code not attributed to a function.

| Object / vtable | Related update | Consequence for the rate |
| --- | --- | --- |
| `FA3FD0 / F08800` | `98EF20`, 8 bytes: calls `433260`, returns | Windows pump to keep responsive |
| `FA3F90 / F03534` | `982AC0` | Host time acquisition, once per host cycle |
| `FA3FF0 / F0AA0C` | `980700` | Inputs and actual deactivation of the interfaces |
| `1140A00 / F07F94` | `998710` | Render time/constants, frame preparation; no Present observed in these 152 bytes |
| `1293880 / F27068` | `84DE60`, registered at `97CBBF` | Synchronous physics or binding to the WinMM owner |
| `11EE900 / F1B8EC` | `87DAB0`, registered at `97CC07` | Online services and idle effects; preserve the polling and the delays |
| `13A70D0 / F0D28C` | `969E70`, registered at `97CCDF` | AI/NPC service, candidate for 60 Hz logic once its side effects are separated |

The initializations and their vtables establish the first two game
bindings. For the AI, `96928F` installs `F0D28C`; the secondary
initialization path contains the reference `Sources/GS_AI.cpp` and loads
`DataBase/AIBotPers.xml`. No commercial database is opened here.

`969E70` caps its delta at `0.06` seconds. It contains state checks, a
delay based on `GetTickCount`, several sub-updates and a conditional
virtual call using `root+20C`. Two sub-calls are conditioned on a
non-zero factor and a non-zero delta. This body is therefore not an
isolated, reentrant NPC function to move onto a free thread. The subgraph
of traffic decisions and its dependencies remain to be typed.

The other services remain to be inventoried before any exhaustive
selection. The stubs `998DB0` and `9A7D80` return immediately with `RET 8`;
`9CFA30`, `999E50` and `9B25A0` have other effects. They must not be
classified as "simulation" by elimination.

The timer is itself a service of the first group: the dispatch arguments
have already been loaded when `982AC0` republishes the globals. Calling the
whole dispatch again with artificial fixed steps while letting this timer
reread QPC would mix explicit times and global times.

## Loss of activation and world pause

In `7BEB90`, the action requests and `16990C3 == 0` take part in a common
condition. At `7BECF0`, `this` is readjusted by `-10`; `7BECFA` therefore
calls the **slot +30 of the world's primary vtable** with 1, not that of
its scene sub-interface.

The constructor `7BE500` installs `F2EFF0` at the primary address and
`F2EFC8` at `+10`. In the base primary vtable, `+30` holds `7BE2E0`: with
1, it selects `MixMenu`, keeps the active non-zero factor in `FA3FB8` if
necessary and writes zero to `FA3FBC`. It then calls `virtual+34`,
including for the other values of the argument. The base callback `7BDBC0`
acts on the UI and writes `world+10320D = 1`.

The observed variants must remain distinct:

| Constructor / state of `97CDE0` | Primary vtable | Slot +30 |
| --- | --- | --- |
| `726EF0` / `0C` | `F388F0` | `7BE2E0` |
| `73EB10` / `05` | `F37250` | `73DFE0`, own checks then base call |
| `76D780` / `1A` | `F33E80` | `7BE2E0` |
| `78EC00` / `06` or `08` | `F326C0` | `7BE2E0` |
| `793880` / `01` | `F32088` | `7934F0`, base call |
| `7B3720` / `14` | `F2FB20` | `7B3290`, **3-byte RET 4** |
| `7B4880` / `09` | `F2FA58` | `7B46D0`, jump to the base |

The 60-byte prefixes of these vtables match between the disks. The first
64 bytes of each of the seven derived constructors also match; they include
their respective primary vptr writes. The current state and its vtable in
memory were not read here. The neighboring code `7B3380` is a distinct
event handler that can also cancel the factor; it is not part of the
`7B3290` body.

A C++ background policy must therefore distinguish the idle cause from
the manual request before the common branch, then preserve the other
conditions and the scene overrides. Merely keeping the `97F260` wait gate open
does not handle this gameplay path. Forcing `16990C3` to one would
also suppress input processing and online effects.

## Physics flags and step owner

The base of the physics service `1293880` gives the following aliases:

| Byte | Offset | Attested writers or effect |
| --- | --- | --- |
| `129BEEB` | `+866B` | Set to one in `7B3450`, reset to zero by `7B39B0` and a branch of `7B4120`; other writes at `8E587F` and `8E808A` |
| `129BEEC` | `+866C` | `84DE60` resets it to zero, then to one when its delta is zero; initialization and helpers also write this field |
| `129BEED` | `+866D` | Constructor `8471E0`, initialization `84D570`, helper at `84DE39`; `97D617` sets it to one during program exit |

`7B3450` handles among other things a camera/script transition; the first
flag therefore cannot be renamed globally as "manual pause". The targeted
scan of relative writes is kept in `physics-flag-write-candidates.json` and
corroborated by the instructions read through Rea.
It does not constitute an exhaustive alias analysis.

The WinMM callback `81EE50` skips integration for `ED != 0` and passes a
zero delta if `EB` or `EC` is non-zero. Passing a zero scene delta
therefore has a concrete effect on the asynchronous physics path. In
synchronous mode, `84DE60` applies the sub-step policy already described in
[retail_focus_timing.md](retail_focus_timing.md).

The worker `81F0B0` installs `timeSetEvent` with `TIME_PERIODIC` then
answers the stop with `timeKillEvent`. The observed flags do not include
`TIME_KILL_SYNCHRONOUS`. The stop acknowledgement and the reentrance guard
alone therefore do not prove the end of in-flight callbacks. The requested
delay is the integer `1000/frequency`: for 90, it is 11 ms. The "90"
setting therefore does not prove exactly 90 callbacks per second; the delta
actually integrated also depends on the modes and checks described.

## Recommended wiring for the reconstructed C++

1. **Host cycle**: keep Windows messages, actual input activation, network
   polling, monotonic time, UI, transitions and deferred work. Produce an
   input snapshot; consume each action edge once. The UI and the network
   delays remain served during a world pause. Device errors keep their own
   path.
2. **60 Hz logic**: extract the world updates and the AI subsystems that
   are actually typed, from the `9B4830` and `97E630` bindings. Do not call
   `97E860`, the timer or the complete `-1` group again. Preserve the order
   of effects, the replay and the barriers until they are rewritten.
3. **90 Hz physics**: choose a single owner. For a first step, keep the
   existing WinMM owner and measure its steps; to replace it, stop the
   producer, join its worker and drain the in-flight callbacks before
   handing the steps to the new accumulator. Do not call `81DFD0` from two
   schedules.
4. **Free presentation**: prepare the render from finished states,
   interpolated if their contract allows it. Call the device and
   presentation cycle once per produced frame. Do not interpolate pointers
   or states still being modified by a worker.

Two distinct monotonic accumulators, catch-up limits and an explicit lag
policy suit the new engine. The time factor and the manual pause apply to
the world steps, without multiplying an already scaled delta a second time.
Network deadlines use an unscaled time. The order of simultaneous 60/90
deadlines must be defined and tested, not assumed equivalent to the
historical scheduling.

Before any hook into the official client, the preprocessing `97C6E0`, the
still opaque services, the active scene lists, the last AI virtual call and
the idle network path remain to be typed. The current facts allow a
progressive C++ extraction; they do not validate a single "60 Hz logic"
hook on a complete game routine.

## Verification to prepare

An independent C++ bench must count 90 physics steps and 60 logic steps per
simulated second with host rates of 37, 60 and 144 Hz, then verify
pause/resume, actual background, transitions, lag, replay and stop during a
callback. Polling and the UI must advance without doubling an input edge or
a transaction. Saturation must remain bounded and visible in the
measurements.

In game, measure separately raw/scaled time, received activation,
presentation rate, integrator inputs, positive delta and actual completion
of the steps. The bounded counters and types appear in the focus/timing
note. A callback counter or a presented frame does not prove the progress
of a vehicle. The menu trials already recorded validate neither driving,
nor the behavior out of focus, nor multiplayer.
