# Adaptive automatic gearbox: new C++ policy

## Status and data used

This controller addresses the improvement requested by Anniiki: upshifts at low
rpm in gentle driving, at higher rpm in sustained driving, consistent pick-up
and downshifts. **It is not a recovered original routine.** The thresholds,
filters and delays below are a new, tunable proposal. The module reuses no
OpenTDU game code and is not wired to any game.

The requirements established by an earlier review were used to avoid two
defects already identified: confusing a gradual release with an abrupt
lift because of a slow filter, and holding a gear close to the cut-off.
That review is not used as a source of original code or constants.

`from_stored_drivetrain()` uses only `Nb_Gears`, the active ratios
`Gear_ratio_1..7` and `Red_Line` of `StoredCarDrivetrain`. For the official
Alfa GT, REF `63518960`, this gives six ratios
`3.5 / 2.25 / 1.52 / 1.16 / 0.97 / 0.82`, and `Red_Line=7000` as the rpm
reference. The provenance is described in [database.md](database.md).
Inertias, weight and the other fields are not used. The common final drive
cancels out in the projection between two gears.

## Controller contract

`AdaptiveAutomatic::create()` accepts a profile independent of the assets;
`from_stored_drivetrain()` provides the data adapter. The active ratios
must be finite, positive, strictly decreasing, between one and seven.
Unused ratios are not interpreted. The reference rpm must be positive and
finite. An inconsistent configuration is an explicit error.

`update()` receives the elapsed time **in seconds**, the normalized pedals
`[0,1]`, the engine speed **in rpm**, the actual forward gear and three
flags: automatic enabled, shift allowed, shift in progress. The pedal is
the driver's demand before any engine cut. A gear of zero indicates that no
forward gear is selected; the module does not choose neutral, reverse or
the first engagement.

The output contains an optional absolute target gear, a reason, the
projected rpm and the diagnostics of the thresholds/delays. The module
actuates neither clutch nor transmission and never modifies the actual
gear. The same request may be repeated; the adapter must not stack these
repetitions as increments. It reports `shift_in_progress`, then the gear
actually engaged. The following inputs recompute the decision with the
actual rpm; no old command is kept in an internal queue.

Each object owns its history, with no global clock and no I/O. Invalid
inputs are rejected before the state is modified. Disabled mode, the
absence of a forward gear and `reset()` clear the driving memory. The
adapter must also reset the controller on a profile change or when the car
is put back on the road.

## Driver demand and shift schedule

A fast response to the pedal and a short driving memory determine the
demand. The exponential filters use the elapsed time, with the following
default constants: pedal 0.10 s rising and 0.20 s falling; style 0.35 s
and 1.20 s. The instantaneous demand can immediately raise the threshold
during a sharp acceleration. The style memory has a bounded weight of 0.45
and does not command a gear directly.

The demand is interpolated by a `smoothstep` curve. Normal upshifts range
from 27 % to 92 % of `Red_Line`; basic downshifts range from 14 % to
48 %. An rpm gap of 4 % and a minimum rpm after an upshift prevent a
decision from immediately preparing its opposite. Braking can bring a
controlled downshift forward; it inhibits normal upshifts above 0.15 of
brake pedal, without extending the lift-off delay.

The projection is computed for each candidate:

```text
RPM_projected = RPM_current × target_ratio / current_ratio
```

It assumes a consistent shaft speed during the shift. Slip, the converter
and the rpm evolution during actuation are not simulated. `project_rpm()`
is public so that the adapter rechecks the request against the actual data
before applying it.

## Kickdown and oscillation prevention

By default, a pedal of at least 0.82 held for 0.12 s qualifies as a strong
pick-up when not braking and below 65 % of the reference rpm. This short
delay prevents an ordinary single-gear downshift from immediately
consuming the kickdown demand before its arbitration.

All lower gears are evaluated with their actual ratios. The selected
candidate is the one whose projection is closest to 80 % of `Red_Line`,
with a projection below the lower of 88 % of that reference and the
current upshift threshold minus the hysteresis. A pick-up can therefore
skip several gears, but no downshift is requested beyond that limit. An
impossible candidate is not replaced by an arbitrarily chosen gear.

An **observed** shift imposes 0.45 s of minimum hold, then a lock against
the opposite movement: one second to shift down after an upshift, 0.65 s
to shift up after a downshift. Very low rpm can bypass the downshift lock
while keeping the projected rpm check. An unchanged strong pedal does
not immediately retrigger a kickdown after a shift; a drop below 0.62
allows a new sequence. Refused or unacknowledged commands do not start
these delays.

## Lift-off, pick-up and high rpm

The abrupt lift detector is separate from the pedal filter. It requires a
drop of at least 0.35 from at least 0.75, within a descending window of
0.20 s with an average slope of at least two pedal units per second. An
event arms a delay of **0.35 s** only once, with no renewal on the filter
lag alone or on braking.

A release from 1 to 0.2 over one second or one and a half seconds does
not arm this delay. A cumulative rise of 0.03 from the minimum cancels the
delay and allows a future event; it also works through small increments.
A sampling interval longer than the window does not allow the detector to
conclude that a lift was abrupt. The bounded windows are a detection
policy, not a continuous observation of every possible gesture.

From the first sample at **92 % of `Red_Line`**, an available upshift
bypasses the driving delays, the brake and the pedal value. The lift-off
delay is cancelled. A pedal at zero or a throttle cut therefore cannot
add this block inside the controller. This priority never bypasses
`shift_allowed=false` or `shift_in_progress=true`. In the last gear, no
extra gear is invented; the real rev limiter belongs to the engine. The
`Red_Line` reference, the actuation latency and these permissions will
have to be consistent in the future integration.

## Validations executed

Successful MSVC x64 build: `/std:c++latest /W4 /WX /permissive- /utf-8`.
The standalone test uses the Alfa ratios above, with no retail bank archive
and no resource constant. **3 640 checks pass**, including:

- gentle and strong accelerations up to sixth gear with deferred
  confirmation by a synthetic adapter;
- kickdown 6→3 from 3 000 rpm, independent projection of
  5 560.98 rpm and refusal of downshifts at excessive rpm;
- grid of 1 296 pedal/brake/rpm/gear combinations, with verification of
  each target and of all downshift projections;
- hold without hunting in a noisy shaft-speed band for ten seconds and no
  immediate return at constant strong pedal;
- gradual releases of 1 s and 1.5 s at 20/120/400 Hz, single abrupt
  events, delay expiry without renewal by braking and gradual pick-up;
- high rpm after lift-off, brake or downshift, external guards,
  transactional errors, resets and single-gear cases.

The runs are synthetic rpm inputs, with a ramp of 1 000 rpm/s and an
acknowledgement of 0.18 s. They verify the policy, not the performance of
a physical vehicle. Each run produces five upshifts, with the following
averages:

| Sampling | Gentle pedal 0.2 | Strong pedal 1 |
| --- | ---: | ---: |
| 20 Hz | 2 445.50 rpm | 6 470.82 rpm |
| 120 Hz | 2 422.22 rpm | 6 444.06 rpm |
| 400 Hz | 2 416.54 rpm | 6 440.63 rpm |

The small overshoot of the threshold in these measurements comes from the
sampling of the ramp. No connection to the original engine, clutch,
converter or vehicle has been tested. Comfort tuning awaits the driving
feel after that integration.

```powershell
.\build\automatic-local\adaptive_automatic_test.exe
```

Local log: `build/automatic-local/validation.log`. No game was launched,
no savegame modified and no file of the old engine imported.
