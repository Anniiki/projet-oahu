# Inventory of OpenTDU changes to carry over

## Reference and scope

State read on October 30, 2026: the earlier OpenTDU-based fork (branch
`recompilation`, HEAD `1edb1fa`), upstream base `573d3d5`. The main tree
is clean, with no stash. The exhaustive register
keeps the 135 local commits and their 152 files, the 42 private branch references
and the 191 additional commits reachable through those references. The latter
include reworked patches; their count does not represent as many distinct
features. The four worktrees are kept.

The register is extracted from Git in UTF-8, without going through the
PowerShell console decoding. The journals supply the scenarios and their
limits: the project state journal, the project recaps, the per-batch work
notes (batches 2 to 7, including the batch 5 driving notes), the design notes
and the project context file. An old journal may describe a state corrected
later; the content of the final commit prevails.

This is an inventory of requirements and historical evidence. The official
executable remains the primary reference; no former engine is imported.
"To carry over" means neither that the official executable suffers from the
same defect, nor that a standalone module has already fixed the commercial game.

## User-facing behaviors

| Domain | Fork commits and changes | State on the new official base |
|---|---|---|
| Display and screens | `45916b4`, `af20bf7`, `d4cc3de`, `0134755`: three modes, per-profile preferences, real pixels/DPI, chosen screen, 256×256 minimum, CLI override. | [Detailed inventory](opentdu_display_inventory.md). Separate Win32/D3D12 backend under validation; wiring into the game required. |
| Background | `c32f8e3`: simulation, sound and gamepads continue without focus; diagnostic options to restore the former pause. | New scheduler with no focus dependency; window, audio and controls of the official engine to be connected. |
| Frame cap and smoothness | `3ea88ff`, `f42d925`, `e11de90`, `75e243a`, `4c06394`, `1eeb9aa`: deadline-based cap, native refresh rate by default, optional native−3, interpolation enabled afterwards. | Display refresh probe and pacing separated from the fixed scheduler. Camera/driver/vehicle presentation to be rebuilt together. No physical VRR proven yet. |
| Physics, logic and NPCs | `4eee075`, `5410300`, `d9c996b`: physics thread measured at 90 Hz. AI and some counters remain per frame in the fork. | [Simulation inventory](opentdu_simulation_inventory.md). Conservative 90/60 Hz scheduler tested at 500 FPS; commercial actors not connected. |
| Automatic gearbox | `9236c0d`, `db08ddf`, `476cde2`, `14e9c54`, `b9747f0`: bounded division by zero, pedal-dependent thresholds, kickdown, hysteresis, hold on abrupt lift-off and engine-speed safety. | New [adaptive](evidence/adaptive_automatic.md) C++ controller, tested with the official ratios. Actuation and the real vehicle remain to be connected. |
| Gamepads | `bd7d42b`, `3c9d643`, `5b4662d`: hot plug and removal, legacy assignments, duplicate devices, capture and simulation lock. | Preserve assignments and lifetime safety; new input system to be built. The native HID fix in the observation module is not this port. |
| Default controls and menus | `743e5ae`, `556e95a`, `ebb529c`: driving detected without initial assignment, directional pad, A/Cross and B/Circle, Xbox/PlayStation legends. | To be rebuilt while respecting user assignments; real gamepad trials still required. |
| Keyboard | `2fc14ac`, `9bcbde6`: key names from the Windows layout and rereading of `DevicesPC.ini` without writing before the action table. | DirectInput/original format to be kept; do not transfer SDL codes blindly. |
| Interface and aspect ratios | `c527553`, `411cdda`, `7838c4b`, `0af7d78`: centered menus, 16:9 zone, HUD at the edges, mouse, consistent masks and gauges. | To be carried over onto the official movies/coordinates. Window decoration alone does not bring these fixes. |
| Profiles and saves | `6e9f1d2`, `f29c05b`, `f63d1a1`: avatar CRC preserved, failure logged then retried with recent state, exit never blocked permanently. | Private saves and source immutability verified; rebuilt save manager still required. |
| Legacy online profile | `2366688`: accepts local loading of a historically online profile. | Local compatibility only. It is neither authentication nor multiplayer; a real online profile requires the server services. |
| Garage and map | `cb3382a`, `7aa6f60`: absent/unknown vehicle handled without crash; absent IGE refused without releasing the current buffer. | Official readers partially available; garage and event flow to be rebuilt. The IGE sheets absent from the DVD remain a separate investigation. |
| GPS and sounds | `c643a23`, `4a1bc37`, `f1dbe1b`, `ff596de`: real bank loading, 17 GPS announcements per language, compatibility with atypical RIFF/chunk sizes. | Banks readable; audio decoder, sequencing and actual listening to be ported. The loading diagnostic is not an auditory validation. |
| Cutscenes | `1f0b876`, `57920b6`, `f7b85a3`, `9fab8e7`, `94025dc`: bank release, announcements in the profile language, avatar voices, stereo resumption after pause and named errors. | To be rebuilt; also include the Bink/intro variants of the private branches before defining the final version. |
| Terrain and streaming | `c97c496`, `617f041`, `4d2265b`: height field built at its actual size, sector installed after complete instantiation, release of zones whose bank is refused. | Resource reading in progress; physics/collisions and sector lifecycle to be established on the official executable. |
| Shutdown | `5a88b81`, `c907bf4`, `32b0013`, `6a22aa4`: process end, cameras/ambience and shutdown of the pipeline compilation threads before destruction. | To be kept as lifetime requirements; the tools only kill their own registered processes. |

The current request adds **borderless and maximum supported quality by
default**. The fork had no attested global "everything at maximum" preset:
night lights in particular fell back to Default. The official options must
therefore be identified, then tested, rather than inferred from the MSAA/aniso
capabilities of the graphics card alone.

## Robustness, performance and tools

These changes are useful for the new architecture; their Vulkan/OpenTDU
structures are not to be copied into the objects of the official engine.

| Family | Main commits | Preserved requirement |
|---|---|---|
| Crashes and logs | `cc2d085`, `a759871`; private-branch variants `f96a401`, `ef2aa14`, `c68e097` | Contextualized local report, symbol/address and faulting access; limit repetitions without losing the counters. No logging of keys or passwords. |
| Isolated trials | `cea50ae`, `d225b1e`, `800c2cd`, `cdb2dc3`, `eb2e64c`, `57c7d7a`, `ed6857e`, `7966402`, `43fed28` | Private saves, explicit virtual controls, scripted mouse/keyboard/pedal, reproducible Continue/New game paths. The new launcher uses the coop-anything workspace slots. |
| Drivers and traffic | `ba18cda`, `5b082de`, `3b74515`, `2d63ecb` and rival/police variants | Controller switch under lock, valid road, measurement of waits and origin of blockages. A diagnostic does not repair the police or the intersections. |
| Time and costs | `2ca8715`, `51d0318`, `3f61b2a`, `258e6eb`, `f8ee1b1`, `c2cee40`, `86bb170` | Duration and count published together, consistent period boundaries, cap wait distinguished, rare costs and visible caches. The wall-clock measurement fix did not remove the old gameplay clamp. |
| Profiling | `8f3478e`, `27b96b2`, `f32cea6`, `51b80de`, `39d4b37` | Call chains, function fragments merged, actual stop of the sampled thread and instantiation profile during waits. |
| GPU resources | `e097e49`, `ae018d7`, `675e391`, `4eb547c`, `b4dc7ef` | Depth formats and mipmaps according to capabilities, exact API version, grouped allocations, uploads without global wait with explicit synchronization. |
| GPU lifetime | `fe0218f`, `e8f7aaa`, `e6ff4af`, `9df28ac`, `a2a0dc5`, `fee8aa5` | Remove all caches/references on destruction of a view, texture, shader or framebuffer; safe deferred destruction on the right fence. |
| GPU diagnostics | `582183f`, `eb7ffe0`, `a0cc5b3`, `65c2ce1` | Markers, device-loss context, race-free publication and framebuffer-change diagnostic. The availability of a debug layer is not assumed. |
| Pipeline cache | `53596dc`, `bd0c52c`, `3406c7b`, `6b86464`, `e5cd681`, `60d08f2`, `e234a9f`, `3650e61` | Stable content-based identities, cache actually rewritten when it changes, prewarmed library, writes retried after failure, diagnosed limits. |
| Asynchronous compilation | `3d000a2`, `a796ead`, `687694b`, `c952f34`, `447da69`, `1fb5f3c` | Wait for the required object without blocking the whole frame; PostFX preserved, instances consumed once, consistent deadlines and accounting. |
| Render reuse | `439ec5d`, `9591304`, `b26ac01`, `a1972e8`, `448e672`, `eb466ea`, `ab85b27`, `d3084b3` | Reuse only when targets, views, states and content are identical; rebuild the passes when their framebuffer changes. |
| Pools and CPU memory | `5faae8b`, `63cadda`, `8be4ea5`, `313c4ba`, `3cbf9a0` | Synchronized access, aligned sub-blocks, reserves sized according to the nodes and audit of the real allocator. |
| Numerical environment | `f5dc2c9` | Controlled floating-point state in each worker; explicit diagnostic exceptions. |
| Build and options | `bb682cc`, `2c7fc18`, `024bebc`, `1edb1fa` | Recent language standard, identified dependencies, consistent code formatting and sufficient CLI capacity. SDL 3.4.18 describes the historical fork, not a newly imposed dependency. |

## Reverts and work outside HEAD

- `85df8ba` was reverted by `912bcf7`: do not restore this temporary
  pause-menu anchoring by presenting it as the last retained preference.
- The radial blur neutralizations `77580c0` and `8aab39f` are reverted by
  `ebae395` and `845f2cd`. The GPU lifetime fix is the reference.
- `refs/clone/*` keeps tutorial/local storage, asynchronous textures,
  batched physics and scenery, zone prefetching, police/rival diagnostics,
  Bink/intro fixes and prewarming options. A patch equivalence does not
  guarantee the integration of all its dependents or its validation.
- `chantier/boite` contains in particular reworks of the lift-off handling
  already present in the main branch; keep the scenarios without duplicating
  the features.
- `chantier/d3d12` (`80357aa`) is a historical prototype: shader compilation
  attested, client linking still failing and clipping to be fixed. It does
  not validate a complete former game running on D3D12.
- `chantier/stabilite` (`789bd2e`) keeps a Vulkan swapchain recovery
  announced as WIP and not compiled. Do not declare it functional.

The JSON register lists every commit and file, including these branches. The
[audit of work outside the main branch](opentdu_alternate_work_inventory.md) covers the
191 identifiers: 97 stable patches, including 74 commits equivalent to the main
history and 43 groups without exact correspondence. It also distinguishes the
adaptations and the v2 fixes kept outside Git, notably the earlier fork's batch
6-B. The specialized inventories keep the evidence, the defects still present
and the wiring conditions. The functional state of the new game is tracked
separately in a dedicated status note.

## Criteria for carrying over onto the official executable

For each family: identify the commercial behavior and its owner, rewrite the
module in readable C++, connect the real data and callbacks, then redo the
scenarios on an isolated copy. Options must not overwrite the user's explicit
assignments or preferences. The original saves remain unchanged. A session
validation failure remains an explicit failure: neither a local load nor a
forced server response becomes a successful multiplayer connection.
