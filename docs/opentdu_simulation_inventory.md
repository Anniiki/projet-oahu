# OpenTDU inventory: simulation, clocks, AI and gearbox

## Baseline and method

Inventory dated 30/10/2026, limited to simulation, clocks, AI and gearbox.
The historical reference is the earlier OpenTDU-based fork, branch `recompilation`,
**`1edb1fa`**, clean at inspection time. Its upstream base is `573d3d5`. The
`refs/clone/*` references and the private branches also preserve work that
was never integrated. They are not confused with HEAD.

Sources consulted: history and diff `573d3d5..1edb1fa`, current sources,
private notes and design documents (`<private-evidence>/notes/`), driving
criteria and the private reports cited below. These historical documents contain provisional
states: later commits are authoritative for what is actually enabled. No
former engine is imported into the new `TDU-Recompilation` base.

This inventory does not cover rendering, interface, sound or network as a
whole; they are treated separately. It records their points of contact with
time and simulation so that no requirement is lost during their
reconstruction.

## Frequencies: proposal, code and measurements

The initial design proposed physics at **120 Hz** and logic/AI at **60 Hz**.
This was not an implementation. The measured milestone `4eee075` reports the
**port** configuration, `PhysicsThread = NORMAL 90`, and about 90 steps/s
between 60 and 175 FPS; ten simulated seconds for ten real seconds within
about 0.1 %. The requested 240 FPS ceiling was not reached in that scene.
The `GamePC.ini` of this project is not evidence of the DVD configuration.

The current code confirms a physics path separate from rendering, but not a
strictly conservative schedule: `gs_physics.cpp:975` uses the measured delta
(`gbPhysicsUseMeasuredDelta = true`), and the thread re-aligns a missed
deadline. `physicsStep` further shortens a delta above `1/45` to `1/90`. The
threadless path subdivides the frame delta and limits the number of steps to
five. Normal measurements therefore prove neither stall catch-up nor
operation at 500 FPS. The AI is still called once per frame.

The new schedule retains **90 Hz physics** as compatibility with the measured
port and **60 Hz logic/NPC** as a new policy, explicitly based on a stable
60 FPS reference. No internal frequency of the official executable is
inferred from these choices. See [evidence/fixed_step.md](evidence/fixed_step.md).

## Changes integrated in the fork

Table paths are relative to the former fork. "To be wired" means that the
requirement is preserved, but that the new client does not yet have all the
systems needed to validate it in game.

| Family and commits | Files concerned / behavior | Historical validation and porting state |
|---|---|---|
| Background `c32f8e3` | `system/ed_sys.cpp`, `tdu_instance.cpp`, README: focus ignored by default for simulation/sound; gamepad allowed in the background; `-pauseonfocusloss` kept as a switch restoring the historical behavior. | Window/gamepads tested according to `RECAPS.md` and the former fork's batch 2. The new schedule does not receive focus; the Windows message pump, sound, gamepads and occlusion remain to be wired together. |
| Telemetry `4eee075`, `5410300`, `d9c996b` | `gs_timer.*`, `physics/gs_physics.*`: frames, physics steps, simulated time, 0/1/2 steps-per-frame histogram and long frames. | Measurements recorded in the private notes (`<private-evidence>/notes/`), directories `reference-4eee075-maxfps-*` and `saccade-eb2e64c-maxfps-*`. New snapshot: accepted time and debt of both domains; histograms/real costs still to be instrumented. |
| Render limiter `3ea88ff`, `f42d925` | `gs_timer.*`, options/profile: fixed deadlines, screen refresh rate and native−3. Does not set the physics step. | Measured 60.0 FPS and 142.4–143.5 for target 144; a screen-synchronized ceiling was later retained by Anniiki. The new schedule never waits for the limiter; wiring to the client/renderer is separate. |
| Costs and diagnostic clock `2ca8715`, `3f61b2a`, `258e6eb`, `f8ee1b1`, `51d0318`, `c2cee40`, `86bb170`, profiler `8f3478e` | `game/gm_base.cpp`, `gs_timer.*`, `core/frame_costs.*`, profiler and renderer: periods closed at the right place, ceiling wait distinguished, unclamped wall time, costs/pipelines. | Driving measurement histories and profiles under `<private-evidence>/notes/`. **The one-second clamp of the gameplay timer was not removed by `f8ee1b1`**, which only corrects the measurement. New schedule: integer monotonic time, no silent bound; full profiler not ported. |
| Interpolation `e11de90`, `75e243a`, `4c06394`, activation `1eeb9aa` | `render/presentation.*`, vehicles `vehicle_base/bike/car/traffic.*`, camera, renderer, `ai_veh_locomotion.cpp`, physics: previous/current/third poses, consistent timestamps, driver drawing reference frame, camera at the same instant, invalidation of discontinuous moves. | Private interpolation patch series and its checker (`<private-evidence>/`); road measurements and the visible driver correction recorded in the private notes. Enabled by default since `1eeb9aa`, despite the older texts saying "disabled". The new schedule provides a common delayed timestamp; pose buffers, reference frames, camera/driver and resets remain to be rebuilt. |
| Floating-point state `f5dc2c9` | `physics/gs_physics.cpp`: floating-point exceptions re-masked in the Jolt workers, diagnostic option `-fpexceptions`. | Real Jolt bench outside the game: masked/unmasked MXCSR and differential exception. New client without Jolt workers: the requirement to control the floating-point environment is preserved, the callback is not imported. |
| Gearbox, division by zero `9236c0d` | `physics_vehicle.cpp`, `vehicle_simulation.cpp`: Police1 case where the peak-power rpm equals the maximum rpm; gauge bounded to 10. | Former `StepGearbox` actually called with exceptions unmasked; three 130 s runs reported without a crash. The new gearbox does not use this quotient; any original solver will have to handle this boundary independently. |
| Adaptive automatic `db08ddf`, `476cde2`, diagnostic `60abd63`, test pedal `3b74515` | `physics/vehicle/physics_vehicle.*`, `vehicle_simulation.*`: threshold on the raw then filtered pedal, kickdown, hysteresis/locks, log of requests/engagements; `-legacyautobox` switch restoring the former behavior. | Criteria C1–C3, `boite03-trafic` series, dedicated driving tasks and rpm readings under `<private-evidence>/notes/`. Carried over as **requirements**, not code: `physics/adaptive_automatic.*`, tests and [dedicated evidence](evidence/adaptive_automatic.md). Standalone controller validated; actuation/physical transmission to be wired. |
| Throttle lift `14e9c54` then `b9747f0` | `vehicle_simulation.*`, `tests/automatic_gearbox/*`: raw detection over a short window, bounded hold and release before the rev limiter; the hold becomes normal behavior, with the former behaviors kept as comparison switches. `14e9c54` re-applies the preserved variant `7632a5b`. | Covered by an earlier review; three C++ suites of the real former code, 468 checks, compiled. No in-game feel validated for the 30/10 resumption. The new controller handles progressive release, pickup and rpm safety with its own documented policy. |
| Test driver `ba18cda`, `5b082de` | `ai/ai_pool.*`, virtual commands: traffic driver, switch under the physics lock and road/lane validated before the controller change. | `-testpad` scripts, driving measurements, an earlier review. Not an AI cadence fix. Human/AI control, ownership and road path remain to be rebuilt; preserve the controller's transaction and lifetime. |
| Intersection diagnostic `2d63ecb` | `ai/ai_veh_nav_traffic.*`, `ai_vehicle.*`: wait reasons, retained object and object that supplied the speed; no change to priorities. | Dedicated task report and `-trafficwaits`. The "stationary object within 50 m" cases and the speed of the last object are behaviors observed in the reference map, not bugs already fixed. New traffic absent: do not announce these blockages as repaired. |
| Terrain `c97c496` | `physics/gs_physics.cpp`: height field built at the useful size, aligned on the Jolt block, instead of the next power of two. | Instantiation profile then terrain A/B criterion. No terrain physics model in the new base: the optimization requirement is preserved, no new evidence that the collisions are identical. |

Purely formatting changes (`024bebc`) create no behavior. Gamepad wiring
(`743e5ae`, other batch 3 commits of the former fork), UI/display and
save games are to be inventoried in their own domains; command sampling must
stay timestamped and command consumption must not be multiplied during
catch-up.

## Work preserved outside HEAD

| References / commits | Content and files | Attested status |
|---|---|---|
| `refs/clone/physique-parallele` → `be12e73`; steps `30abd04`, `6ae05d0`, `d49a780` | `physics/gs_physics.*`, `world/sector_manager.cpp`, costs: preparation of SR20/world/terrain collisions on external tasks; addition to the world unchanged; sequential fallback when launch is impossible and a guard that the physics system is present. | An earlier review, criteria P1–P6. Provisional report: events 17.88→5.45 ms on very small samples; P6 not held to the letter, A versions crashed on shadows/pool, repetitions missing. Not integrated into `1edb1fa`, not ported to the new client. |
| `refs/clone/decor-par-lot` → `486c256`, variants `ac6706d`, `202e451` | `physics/gs_physics.cpp`: grouped removal of a sector's scenery bodies. | Scenery A/B criterion and dedicated series; to be separated from the parallel preparation. Not present in the inventoried HEAD; not ported. |
| `refs/clone/police-diagnostic` → `d0c677b`, `refs/clone/mesure-police` → `e5b7f54`; variants `518de64`/`4807757`/`24efbcc`, `5fa70ff` | Diagnostic of the stationary police officer and origin of the navigation loss, in the test clones. | The observations do not constitute a repair of the police AI. Preserve before rebuilding the controllers. These variants are not in HEAD and are not new frequencies. |

The duplicated hashes are applications of the same patches on different
measurement bases, not as many distinct features. The worktrees and
references must be preserved; no engine cherry-pick is needed to reproduce
these requirements in the official base.

## FPS dependencies still present in the former fork

These points already exist in the upstream base and were not repaired by the
commits in the table:

- `ai/gs_ai.cpp:154` clamps the delta to `0.06f`, then distributes the logic
  every frame; `AiPool::updateOpponentRoundRobin` uses a cycle of eight
  calls. At 500 FPS, this cadence does not spontaneously become 60 Hz.
- `ai/ai_bot_manager.cpp` counts 1000 calls before revising the population;
  `game/game_mechanism_player.cpp` uses 210 calls to reactivate the bots.
  The new 60 Hz domain will make it possible to preserve reference durations
  of 16.667 s and 3.5 s, but these functions are not wired yet.
- Missed checkpoints, off-road tolerance (five calls), countdown, traffic
  lights, spawns and the random generator must consume the logic ticks in a
  stable order. Unproven durations remain counters, not invented seconds;
  see the former fork's batch 5 design for the catalog.
- `GameMode::mainLoop` calls logic and drawing once per frame; the timer is
  itself a service, while the arguments of `stepLogic` come from the previous
  timer. The telemetry fix does not separate these phases.
- Gearbox commands are events, pedals are held states. A timestamped queue
  must keep a press on a frame without a tick and consume it exactly once
  during a catch-up; rereading the hardware at each sub-step would alter the
  inputs according to the cadence.

## State in the new base and wiring criteria

The new `game/fixed_step.*` produces the 90/60 Hz deadlines independently of
frames, preserves the debt and distinguishes explicit pause from wall time.
Its C++ test checks 20/60/144/500 FPS, stalls, overload, simulated
focus/occlusion, pause, ordering of the two domains and clock limits. It
contains no vehicle, collision, road or NPC model taken from the port.

The adaptive automatic and the official data readers exist independently.
Their existence is not evidence that the car, the NPCs, the missions and
their rendering are already reconstructed. The integration step handles the
CMake wiring and the overall status; these modules are delivered without being
staged in Git.

To carry over every requirement, the following still has to be connected:
timestamped commands → 60 Hz logic/AI/counters → 90 Hz
forces/transmission/collisions, preserving deterministic order and a single
speed policy; publication of timestamped poses; common interpolation of
camera/driver/vehicles without writing into the simulation;
physics/sound/vibration events delivered exactly once.

The system loop must keep pumping events, network and ticks when rendering is
occluded, minimized or unfocused. A "no frame" return from the renderer must
not skip these phases. An irreversible GPU failure requires its diagnostic
and an explicit exit, not an invisible infinite wait. A pause requested by
the engine may suspend the simulation, never its real network clock. Under
overload, the debt stays visible and is drained in batches with
event/network servicing between batches; it is neither erased nor relabeled
as simulated time.

A final validation will have to measure the real reconstructed engine at the
same simulation instants: race times, position, speed, trajectory, NPC counts
and states, consumed commands and network counter; Anniiki's captures and
driving impressions kept separate. **194104 checks of the synthetic schedule
do not prove driving faithful to the commercial game, nor NPCs already
independent of FPS.**

## Reproducing the inventory

From the preserved fork, read-only:

```powershell
git log --oneline 573d3d5..1edb1fa -- source/physics source/ai source/gs_timer.cpp
git diff --stat 573d3d5..1edb1fa -- source/physics source/ai source/game source/render/presentation.cpp
git for-each-ref --format='%(refname:short) %(objectname:short) %(subject)' refs/clone
git show 4c06394
git show f8ee1b1
git show b9747f0
```

No game, official executable, save game, former source tree or Git reference
was modified for this inventory. Historical validations are reported from
their logs, without replaying them.
