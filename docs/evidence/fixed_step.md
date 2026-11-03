# Fixed physics and logic schedule

## Origin and status

New C++latest infrastructure answering Anniiki's request: physics, simulation
and NPCs independent of the frame rate, including 500 FPS, and no implicit
pause on focus loss. **This is not a recovered original
routine.** No code from the OpenTDU engine is imported.

The [historical inventory](../opentdu_simulation_inventory.md) distinguishes the
120 Hz proposal from the actually measured path of the port at 90 Hz, and
confirms that the AI and several counters remained tied to frames there. The
historical defects of lost time do not become requirements to copy.

Default policy: **90 Hz physics**, compatible with the measured port;
**60 Hz logic/AI/counters**, a new choice based on the reference behavior at
60 FPS. These values are not attributed to the official executable. The
configuration is explicit and immutable once the schedule is created.

## Contract and exact time

`FixedStepScheduler::advance(elapsed, explicitly_paused)` accepts a monotonic
interval in nanoseconds. A negative duration or a counter capacity overflow is
an error, with no partial mutation. A long interval is not clamped to 1 s or
to a few steps. No accumulator clamp exists.

The LCM of the two frequencies forms a common rational schedule: 180 quanta/s
for 90/60 Hz. Physics deadlines are 2, 4, 6… and logic deadlines 3, 6, 9…
quanta. `next_tick()` emits the oldest deadline; on a tie, logic/AI precedes
physics. This priority is a new policy, documented so that same-dated commands
are supplied before their integration.

The duration of a tick is always `1/frequency`. The conversion of the received
time divides seconds/remainder **before** multiplying, without a floating-point
accumulator or repeated rounding to 11 111 111 ns. Deadlines and ordering are
integers; `double` is used only at the output for numeric consumers and
rendering.

To guarantee 64-bit products, the accepted LCM is at most 1 000 000 000 Hz.
This is an arithmetic bound of the API, not a recommended game rate. Accepted
wall time is bounded by `nanoseconds::max()`; an overflow returns
`elapsed_overflow`, without dropping time presented as accepted.

`next_tick()` **reserves** the tick and advances its counter. The caller must
execute it exactly once and in that order on its simulation thread. This module
does not handle a simulation operation that may fail, nor a transactional
rollback; do not request a tick and then decide to ignore it. Indices are
specific to each domain and start at one.

## Debt, pauses and network

The caller can bound the work of one batch, for example eight events. The
snapshot exposes separately the physics and logic ticks still due. After a 2 s
stall, 300 events are due; processing eight leaves 292 events, with no loss of
duration. Subsequent batches can consume them without a new frame or an
artificial addition of time.

Sustained overload requires an explicit service policy: pump events and network
between small batches, continue catching up, publish the debt and avoid a
presentation wait that would needlessly increase it. This module does not
guarantee that a computer that is too slow will catch up with real time. It
guarantees that the lag is not silently erased or called normal speed. Any drop
of the debt or resynchronization with the server would be a separate policy, to
be announced and measured, absent from this API.

The explicit pause applies to the supplied interval: wall time advances, active
simulation time does not. The tick fraction and the debt already due are
preserved, but `next_tick()` emits nothing during this pause. Resuming with
`advance(0ns,false)` reactivates their processing. The caller must split an
interval at the real date of a pause event, instead of arbitrarily attributing
a whole long frame to the new state.

The schedule receives no focus or occlusion flag. Losing focus therefore does
not become a pause by accident in the module. The wall-clock snapshot lets the
engine keep its own independent monotonic network time; it is neither a UTC
timestamp nor a network format to be transmitted between machines. edNet
delays and sequences remain governed by their own reconstruction, not by an
invented 90/60 Hz send rate.

Non-unit simulation speed is not implemented here. The former `GameSpeed` and
`gPhysicsTimeScale` combinations, or `FIXED_TIME` per frame, are not implicitly
reintroduced. A future slow-motion policy must transform active time once and
preserve network time.

## Presentation and real hookup

Once both domains have caught up, the snapshot provides a **common presentation
date**: active time minus the longest step, that is 1/60 s by default. It
remains absent during debt or before the first complete interval. Systems must
keep dated poses that bracket this date; it is not a universal alpha between
two poses. With 90/60 Hz, three physics poses and two logic poses may be
needed. Larger buffers may be required with other frequencies or slow
publishing.

The camera, the driver and the car must share the same date and the same frame
of reference. A teleport, a reset onto the road or a streaming origin must
invalidate the affected pose pair, without resetting the network clock.
Presentation never writes into physics poses.

Hookup sketch, **not a retail loop already delivered**:

```cpp
// The engine supplies now from a single steady_clock, pumps events,
// and splits pause changes and timestamped commands precisely.
auto accepted = calendar.advance(now - previous, explicit_pause);
// Handle a duration error explicitly; never continue by masking it.
for (unsigned work = 0; work < 8; ++work) {
    const auto tick = calendar.next_tick();
    if (!tick) break;
    if (tick->domain == SimulationDomain::logic)
        update_gameplay_ai_npc(tick->index, tick->delta_seconds());
    else
        integrate_physics(tick->index, tick->delta_seconds());
}
// Network/events remain serviced. If the debt persists, schedule another batch.
// Render only when the window/GPU and poses are available.
```

Input events are consumed once, at the chosen tick; pedals remain held between
events. The gearbox controller receives the consistent physics step and the
gear actually engaged. Logic/NPCs, collisions, transmission and solvers still
have to be hooked up: a schedule does not reimplement them.

The application loop must advance simulation and network before any
"frame unavailable" exit. Minimization, 0×0 resize, occlusion and focus loss
may suspend drawing, never remove the servicing of ticks. Avoid an indefinite
`Present`/GPU wait on the sole simulation thread; the pump/pacing policy must
remain bounded and responsive. An unrecoverable GPU loss is handled as an
explicit error. No window, out-of-focus sound/gamepad handling or Windows
scheduling proof is created here.

## Executed validation

Compilation of the two real C++ files under MSVC 19.44 x64 with
`/std:c++latest /W4 /WX /permissive- /EHsc /O2 /utf-8 /MP2`.

```powershell
.\build\fixed-step-local\fixed_step_test.exe
```

Result: **six scenarios, 194104 checks passed**.

| Test | Result |
|---|---|
| 120 s at 20/60/144/500 FPS | 10 800 physics ticks and 7200 logic ticks at each rate; analytic positions, states of eight synthetic NPC actors, population events and ordering hash strictly identical. |
| 90/60 deadlines | Exact sequence over 100 ms, logic before physics at coincidences; no variable dt. |
| 2 s stall and per-batch bounded debt | 300 events preserved; eight consumed then 292 visible, finally caught up without a new frame. |
| 15 FPS overload for 4 s | 120 backlog events preserved then drained; final state identical to 20 FPS without overload. |
| Long 72 h interval | 23 328 000 physics ticks and 15 552 000 logic ticks due, without a one-second clamp. |
| Alternating focus/occlusion at 500 FPS | Presentations skipped in the test adapter; same states/ticks as the visible pass, without focus used as a pause. |
| Explicit pause | Wall time maintained, active time/fraction/debt preserved; no debt created by the paused seconds. |
| Arithmetic boundaries | 11 111 111 ns does not prematurely trigger a 90 Hz tick; one second produces 90/60 ticks; overflow, negative values and invalid frequencies rejected without mutation. |

The actors are independent deterministic fixtures and a constant-acceleration
integration whose expected position is analytic. They are not the reconstructed
retail physics, traffic or NPCs. No game data, save or original executable was
modified or launched. Validation of the real engine at 500 FPS and without
focus remains to be performed after hookup. The three C++ files comply with
`.clang-format`, checked with clang-format 19.1.5 in `--dry-run --Werror` mode.
