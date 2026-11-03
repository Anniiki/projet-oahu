# SDL3 gamepad transport

## Scope and source

This new C++ client module transports SDL devices and controls; it
reconstructs no TDU binary routine. It defines neither throttle nor menu
confirmation nor map behavior. The complete 360 interface and context
port remains separate from this transport.

On October 30, 2026, `gh api repos/libsdl-org/SDL/releases/latest` returns
[SDL 3.4.18, released October 2](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18).
Only the integration step acquires the official ZIP, SHA-256
`9cd42377704398796071b8597cd7e21da254a43bad98c4199739647adc13fa6f`.
Private builds use its `SDL3::SDL3-static` CMake exports, with no further
download or reused SDL2/OpenTDU code. SDL uses the
[zlib license](https://github.com/libsdl-org/SDL/blob/release-3.4.18/LICENSE.txt).

## Usage contract

`SdlGamepads::create()` initializes **its own reference** to
`SDL_INIT_GAMEPAD`. Handles are closed before the corresponding
`SDL_QuitSubSystem`; no global `SDL_Quit()` is called. The module does
not manage the host's EVENTS/VIDEO/AUDIO references. Initialization,
methods and destruction require the same SDL main thread. Methods reject
another thread before effects; destruction outside the contract terminates
the program rather than calling a non-thread-safe SDL shutdown. These
rules follow [SDL_init.h](https://github.com/libsdl-org/SDL/blob/release-3.4.18/include/SDL3/SDL_init.h).

The host retains its `SDL_PumpEvents`/`SDL_PollEvent` loop and forwards
every `SDL_Event` to `handle_event()`. The module never pumps, dequeues,
filters or flushes this queue. Window, audio and keyboard events remain
available to their other subscribers. Destroy the transport before the
host's final global shutdown.

`controls_snapshot()` is the normal path: owned fixed storage for at
most 64 connections, without names or allocation on success. `snapshot()`
copies metadata only for configuration or a device change. Configurable
quotas bound connections, names and mapping text; reconciliation failures
retain the previous state. Explicit removal neutralizes immediately,
without depending on another enumeration.

## Connections, remapping and profiles

Each connection has a 64-bit token specific to this transport instance,
never an address or persistent player identity. Joysticks not recognized
as gamepads remain visible with `mapped=false`. GUID, VID/PID and name
are copied; no serial number is queried. A GUID does not necessarily
distinguish two identical devices: the host retains its profiles.

ADDED/REMOVED/REMAPPED events allow opening, removal and updates without
restarting. Returned resets contain a fully neutral state and a reason;
on remap, the host releases previous actions before consulting controls
under the new mapping. Raw axes remain int16; normalized values follow
sticks `[-1,1]`, triggers `[0,1]`. UI activity thresholds do not alter
these driving values.

Types, real type and labels come directly from
[SDL_gamepad.h](https://github.com/libsdl-org/SDL/blob/release-3.4.18/include/SDL3/SDL_gamepad.h)
functions. SDL supplies labels only for face buttons. UNKNOWN, extra
buttons and unknown families remain explicit; no TDU icon or action is
invented for them.

`load_mappings_text()` performs an **explicit in-memory merge**, without
resetting SDL's database or writing a profile. Each line must carry a
field such as `platform:Windows,`: SDL ignores lines without a platform
or for another platform. The returned counter counts new mappings,
not all updates; zero therefore does not prove no change. In the pinned
source, the loader returns negative before its loop if loading the full
buffer fails; line errors are ignored. A rescan failure **after** merging
carries `mappings_may_have_changed=true`. This is not a transaction with
global-database rollback.
[Official loader implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/joystick/SDL_gamepad.c).

## Display priority and initial events

Initial mode is keyboard/mouse. Connection, rescan, remap and sampled
values alone do not activate gamepad mode. A new press, stick/trigger
crossing hysteresis or deliberate movement changes the last device.
Small mouse return movements cancel out; successive 1 px packets and
wheel fractions accumulate into bounded net displacement. Significant
gamepad activity resets this anchor even if the mode was already gamepad.

Observed-event history is separate from SDL getters: these may already
contain a press whose event is queued after ADDED/REMAPPED. Logical/joystick
duplicates are deduplicated by state and a fixed history of 64 activity
timestamps per connection, without assuming numerical ordering.

Before the first `SDL_Gamepad` opening, SDL may have generated only
JOYSTICK events. For a recognized device, the transport then prepares,
outside the hot path, physical controls present in `SDL_GetGamepadBindings`
and their `SDL_GetJoystickAxisInitialState` initial state. These events
serve only initial UI activity; logical controls remain SDL's. Once
the GAMEPAD stream is observed, it becomes the activity reference.
A released raw trigger at `-32768` is not mistaken for a press.

Demonstrated limit: if an axis is already 24000 at the **very first**
sample, SDL may return `initial=current=24000` without a GAMEPAD event.
The transport exposes this held value, but does not invent an earlier
neutral point. The separate test first establishes SDL neutral without
dequeuing ADDED, then applies movement and pumps before handling: this
initial activity is detected.
[Official joystick contract](https://github.com/libsdl-org/SDL/blob/release-3.4.18/include/SDL3/SDL_joystick.h).

## Background operation and validations

No focus-loss event stops or neutralizes this transport. SDL event
generation remains a **host/process** policy:
`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` normally equals `"0"`; the
new client's window must explicitly select `"1"` for background controls.
The module does not silently change an existing host's global hints.
[Hint contract](https://github.com/libsdl-org/SDL/blob/release-3.4.18/include/SDL3/SDL_hints.h).

`sdl_gamepads_test.cpp` uses real SDL virtual joysticks: Xbox/PS3/4/5
families, labels, unmapped→mapped, held remap, reconnection, the actual
SDL queue, noise/hysteresis, sensitivity, synthetic focus, quotas, errors
and threading. Counters observe `operator new` and SDL malloc/calloc/realloc
hooks on the owner thread around measured calls; they do not cover the
pump, OS drivers or other threads. No global SDL zero-allocation claim
is made. Initial failing logs remain in
`<private-evidence>/reprise-2026-10-30/sdl-gamepads-cpp/`.

Private Release x64 and Win32 builds pass `/std:c++latest /W4 /WX` with
both official SDL3-static libraries prepared beforehand. Each
architecture's test passes **1 011 checks**, linked version `3004018`.
Measured controls/buttons/fixed-snapshot and virtual-removal calls count
zero C++ and SDL allocations on their thread. Preserved initial failures
document the mapping line without platform, lost activity from confusing
samples with events, then the axis moved before its first sample.
The last case is explicitly qualified without fabricating history.

The integration adds `tdur_sdl_gamepads` and CTest `sdl_gamepads_virtual`.
Explicit target construction and **50/50 x64 CTests** pass; the integrated
Win32 test also passes with the same SDL3.4.18. Parent evidence is in
`native-input-async-integration-v1/` and
`sdl-gamepads-cpp/parent-integration-win32-v1/`.

### Virtual test-bench isolation

The `vmf-sampler-parent-integration-v1/` continuation reveals a fixture
defect: its first pump also received a system JOYSTICK_AXIS_MOTION event
(instance 1, axis 0, value -25932) before any virtual joystick creation.
The neutral-start assertion therefore did not control all its inputs.
The first batch retains its **52/53** failure with diagnostics and logs.

Only the test executable now disables the seven physical Windows backends
through process-local hints before initialization and requires empty
initial enumeration. A separate case then creates an actual neutral
virtual joystick **before** a second facade, leaves ADDED queued and
checks keyboard/mouse mode before and after handling it. Activity,
remapping and removal oracles remain required. No change to the transport
or its production drivers is necessary.

The fixture now counts **1 060 checks** per run and passes five consecutive
times on x64 and five on Win32. All four measured allocation counters
remain zero. The final integrated batch passes **53/53 x64 CTests**;
these results are retained alongside the initial failure.

These fixtures validate neither a physical controller nor an actual
focus change. No rumble, LED or effect is requested. Driver compatibility,
real usability, user calibration and complete 360 behavior still require
separate verification; virtual tests do not replace them.
