# Startup area and sector preparation

`startup_service_preparations.hpp` reconstructs `708D50`, `7053E0` and their
coordinate helpers `706C00` and `704520`. `PreparedServiceCyclePorts` binds
these bodies to the existing `9B4120` service cycle, preserving its captured
object identity and refusal propagation through the mode controller.

## Primary evidence

Reference: official PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
All four bodies are above `6DD000`. The existing Ghidra exports were read
first, then bounded disassembly of the unchanged official PE established
register inputs, instruction order and signed branch conditions. No original
instruction was executed and no new analysis session was opened.

| Body or data | Bytes inspected | Established behavior |
| --- | ---: | --- |
| `708D50` | 196 | Guarded center and three neighboring areas |
| `7053E0` | 226 | Guarded center, drain, then six sector slots |
| `706C00` | 272 | Eight area directions, including boundary sentinels |
| `704520` | 511 | Six sector directions with area-boundary crossing |
| `706D10`, `704720` | 32 each | Exact direction-to-branch jump tables |
| `F9992C` | 320 | Eight observed rows of ten direction words |

The local record contains these seven byte-window hashes, seven atlas-file
hashes, both jump tables, the direction words and equal before/after image
hashes. The loading-helper exports identify area/sector terminology through
their path formats. Their callers now have the explicit binding described
in [world loaders](startup_world_loaders.md); their leaf calls remain required.

## Area preparation

`708D50` tests the entire byte `+13`. A nonzero value leaves all state intact.
Otherwise the two signed coordinates supplied in EAX choose rows `4..7`:
`(first > 3 ? 4 : 5) + (second > 3 ? 2 : 0)`. The driver stores that row,
the data identity at `+3770`, and its center member identity at `+3764`, then
calls `708C80` on member `+2690` with the original area coordinate pair.

Three iterations load the direction at `F9992C + (row * 10 + i) * 4`, call
`706C00` and call `708C80` on `+20 + i * CD0` with zero flags. The row, table,
source coordinates and dimensions are read again after loading callbacks.
Byte `+13` becomes one only after all three returned calls.

`706C00` checks only the moving axes and their relevant boundary. A failed
known direction writes `(-1,-1)` but returns AL=1. Unknown directions return
AL=0 without modifying the output. The caller ignores AL: a previous stack
pair survives an unknown direction. On the first iteration, however, such a
pair is uninitialized; the modern driver refuses instead of inventing one.
The ordinary primary rows `4..7` contain only known directions in their first
three entries. A table access outside its supplied view is a modern refusal.

## Sector preparation

`7053E0` tests byte `+0B`, stores the data at `+76604` and center identity at
`+76600`, calls `705190` on `+65770`, then calls `9A4930` with ESI identifying
`10E80C0`. The drain operation is required and has no successful default.

The loop examines directions `0..7` with a separate slot counter capped at
six. Directions `2` and `3` return AL=0 without writing the stack pairs and
consume no slot. For the other directions, AL=1 calls `705190`; AL=0 writes
only the slot's DWORD `+2B4 = 3`. Both cases consume a slot with stride
`10E90`. Completion writes `+0B = 1`, `+08 = 1`, then `+09 = 0`.

`704520` selects east, west and four diagonal neighbors. Diagonal horizontal
movement depends on the local row parity. A crossing updates the area pair
and wraps the sector coordinate. Straight east uses a signed `>=` boundary,
whereas diagonal east uses equality; these remain distinct for malformed
coordinates. Unsupported directions preserve both outputs. A failed known
direction replaces both with `(-1,-1)`.

The area-dimensions link at `+76608` is supplied by the host, not invented by
either preparation. It is required only on paths that actually read it.
A missing link produces a modern refusal and retains earlier output writes.
Both helpers implement 32-bit increment/decrement wrapping without signed
C++ overflow. Their value inputs model the separate source/output storage
used by these two callers; arbitrary aliasing in other callers is unproven.

## Composition and ownership

`ServicePreparations` owns the coordinate pairs and bounded area/sector
records. This is a modern representation, not a native memory image.
Default field values do not establish native constructor behavior. The host
initializes dimensions and existing links and keeps objects and views alive.

The binding associates one `ForwardCycleObject` with this state explicitly.
It checks every object/member identity supplied by `9B4120` before entering
the corresponding driver. Changing the global object during a loading call
does not change that association. Required loading and allocation-release
ports remain explicit. Their refusals stop the driver, final guard writes,
the caller's reset/ready writes and further mode-controller callbacks.

## Validation and limits

The focused suite checks all positions of a `4 x 3` area grid with `8 x 8`
sectors against an independent global-coordinate neighbor oracle. Fixed
vectors cover area edges, unsupported directions, signed 32-bit extremes,
missing-link paths and the diagonal equality condition. Driver cases verify
slot order, untouched record data, live callback mutations, guards, all
twelve loading/drain refusal points and mismatched member identities.

Composition runs the actual mode controller, `Service9b3f80`, `9B4120` and
the new binding; required leaf calls are test witnesses. Build
`startup_service_preparations_test` and run CTest with
`-R '^(startup_service_preparations|startup_service_cycles|game_mode_lifecycle)$'`.
The retained validation records the x64 Release build, `/W4 /WX`, one job,
BelowNormal priority, the shared build mutex and the 6 GiB child guard.

The [world loader binding](startup_world_loaders.md) connects the bodies
`708C80`, `705190` and `9A4930` to this preparation interface. Their required
leaf operations, allocator ownership and complete host installation remain
unresolved. These tests establish preparation behavior and composition, not
a loaded world, rendering, a playable client or multiplayer.

Related: [service cycles](startup_service_cycles.md),
[mode lifecycle](game_mode_lifecycle.md).
