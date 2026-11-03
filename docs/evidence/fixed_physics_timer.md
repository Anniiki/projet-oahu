# Standalone fixed-step physics owner at 90 Hz

## Result and scope

[fixed_timer.hpp](../../include/tdur/physics/fixed_timer.hpp) and
[fixed_timer.cpp](../../src/physics/fixed_timer.cpp) provide a schedule
owner with an injected clock and integrator. It accepts irregular
notifications and preserves the debt and the fraction exactly between
notifications. Each completed step passes the same float `1/90`, IEEE
pattern `3C360B61`, then **+0.0** as the second argument to the integrator.

This module is a **new policy**, not an equivalent rewrite of the legacy
callback. It creates no timer or thread, does not sleep, accesses no
process and contains neither hooks nor a Havok model. It does not depend
on any presentation cadence. The future adapter must keep WinMM as the
only authorized producer of notifications; the module does not itself
identify their originating thread or API.

The `LegacyTimerClock` and `LegacyTimerIntegrator` interfaces are shared
with [legacy_physics_timer.md](legacy_physics_timer.md). Only the clock's
QPC method is used. Neither `milliseconds()`, nor the native globals,
nor their 100-sample profiling are consulted or modified.

## Legacy basis and intended differences

The ABI, flag and owner contracts observed in the official PC MC 1.66 A
executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`,
are documented in [retail_physics_owner.md](retail_physics_owner.md).
The original routine `81EE50/596` replaces its last QPC with the real QPC
after admissibility; it keeps no catch-up debt. The worker requests a
whole WinMM period of 11 ms for a frequency set to 90.

| Topic | Reconstructed legacy | New owner |
| --- | --- | --- |
| Call cadence | Depends on wake-up, half period and delta mode | Exact 90 Hz deadlines, independent of wake-ups |
| Delta passed | Measured, period or zero depending on flags | Always the constant float `1/90` for a positive step |
| Lateness | No debt kept | Whole debt visible, budget per notification |
| First QPC | Initialization mixing MS delta and ticks | QPC base explicitly sampled by `begin` |
| QPC going backwards | Modulo 64-bit subtraction | Generation faulted |
| Reentrancy | Test/write of a native byte | Atomic guard shared by the schedule and the lifecycle |

`F8A2E8` is not a parameter of this policy: it produces fixed steps from
QPC time. The clamp and the factor applied by the real integrator are not
reimplemented here. Test comparisons use synthetic integrators, without
inferring a vehicle's displacement from them.

## API and generations

`FixedPhysicsTimer({qpc_frequency, max_steps_per_callback})` requires two
non-zero parameters, otherwise it throws `invalid_argument`. The frequency
is immutable for the object; it must match the injected clock. The default
budget of five is a choice of this policy, not a property attributed to
the legacy WinMM callback.

- `begin(generation, clock)` requires an inactive owner and a non-zero
  64-bit token strictly greater than the last accepted token. It samples
  the base, including the value zero, then resets debt, fraction and
  counters. A failed read does not consume the token.
- `stop(generation)` closes admission and returns a receipt of the
  abandoned debt steps and fraction. It keeps the counters and the last
  fault for inspection. Repeating a stop of the same inactive token
  succeeds with a zero receipt. An old token cannot stop the new
  generation.
- `notify(generation, clock, integrator, flags)` refuses invalid, old,
  inactive or faulted tokens before any clock or integrator call. Its
  result contains the steps completed by this call and a consistent
  snapshot.
- `try_snapshot()` provides a non-blocking copy; it may be empty during an
  operation. Prefer the snapshot included in `notify` to observe without
  retaking the guard.

Tokens are allocated by the caller. They are neither `uTimerID` nor a
Windows handle assumed to be unique. The adapter must keep the token of
the arming that produced each event; it must not reclassify an old
callback with the current token. If a reused native ID no longer
distinguishes events, an immutable context or a prior drain is needed.
The standalone module does not resolve this native attribution. A maximal
token does not wrap: any following token that is not greater is refused.

## Integer arithmetic and visible debt

For a QPC frequency `F`, the snapshot contains `U` whole steps of debt and
a numerator `R`, with `0 <= R < F`. This fraction is **a fraction of a
step**, `R/F`, not a number of seconds or of remaining QPC ticks.
For an accepted interval of `d` ticks:

```text
new_steps = floor((90 * d + R) / F)
new_fraction = (90 * d + R) modulo F
new_debt = U + new_steps
```

The code does not form the potentially overflowing product `90*d`: it
separates the whole seconds, then performs at most 90 modular additions
of remainders, subtracting before adding. It uses neither floats nor a
non-standard 128-bit integer type for the schedule. The frequency and the
remainders are covered up to `UINT64_MAX` in the tests.

The debt and the total of completed steps are checked before time is
committed. The interval is accepted whole or faults the generation
without modifying the previous debt, fraction or QPC base. The number of
executed steps is then limited by the budget; any remaining debt is kept.
A repeated QPC adds no time, but **may pay a debt already accepted**.
Once that debt is exhausted, repeating this QPC produces no further step.

This budget bounds a notification, not a frame. No admissible long delay
is capped or silently dropped. If the integrator cannot keep up, the debt
stays visible; this module does not decide to skip world state to hide
that lateness.

## Pause and resume

Flag names stay raw, without equating them to a menu or to focus:

| Flags supplied to an accepted notification | Effect |
| --- | --- |
| `ED != 0` | No integration call; debt and fraction abandoned |
| Otherwise `EB != 0` or `EC != 0` | One `(0,0)` call; same abandonment of debt and fraction |
| All zero after an observed pause | New QPC base, no step and no time added |
| All zero while running | Rational accumulation and steps bounded by the budget |

The first pause notification conservatively describes the whole interval
since the previous accepted notification. The abandonment receipt covers
the debt and fraction **already present**, not this new ignored interval.
Subsequent pause notifications create no debt. Zero-delta calls are not
counted as positive steps.

On resume, the exact instant of the change is not provided by this API.
The first unpaused notification therefore serves only as a base: up to
one resume interval is not simulated. This loss, bounded by the next
notification, is a deliberate choice, different from the strict
preservation of time during ordinary intervals.

A control transition that is not delivered or is refused with `busy` cannot be
guessed afterwards. The adapter must serialize and retry these commands.
Flags are a per-call snapshot; native globals that would change during a
burst are not automatically resynchronized by this model.

## Concurrency, stop and lifetime

All operations use the same non-blocking atomic guard, held during the
injected calls. The operation that obtains the guard runs to completion;
a concurrent or reentrant one returns `busy` without reading the clock,
modifying state or integrating. No operation is queued implicitly.
The snapshot copy shares this guard and may briefly cause a refusal; the
result of `notify` avoids this extra observation.

**`stop=busy` is never an acknowledgement of a stop.** The adapter must
retry it after the active operation ends, before any teardown.
A successful stop establishes that no internal call remains active at its
commit point and refuses the subsequent notifications of this generation.
A stop arriving before a notification refuses it; arriving during it, it
returns busy and does not interrupt it mid-integration.

This removes neither the WinMM producer nor its future calls. Before
destroying the object or unloading its code, the adapter must close that
producer and guarantee the lifetime of the object, the clock and the
integrator for all calls still possible. The native drain contract
remains distinct.

## Errors and uncertain external effects

The exceptions handled here are **C++** exceptions, under `/EHsc`. The
module does not guarantee recovery from Windows SEH exceptions raised by
a native Havok integrator; that adapter remains to be established.

A C++ exception from a QPC read, a QPC going backwards or an arithmetic
overflow locks the generation in a faulted state. The subsequent refusals do not
reread the clock and retry nothing. A QPC going backwards includes a
counter wrap; it is not converted into a very large positive interval.

If the integrator throws, it may already have modified an external world.
The steps previously returned normally remain committed. The failed
attempt is identified as `fixed_step` or `zero_delta`, the number of
attempts stays distinct from the completed steps, and the generation becomes
faulted. For a positive step, the debt still contains this unconfirmed
step: **it no longer guarantees that the step was not applied**. It is
never replayed automatically.

Only a stop followed by a begin with a new token allows new calls. This
new generation does not repair external effects: restoring domain
consistency belongs to the caller. Token errors and `busy` do not fault
the valid generation.

## Validation performed

[fixed_timer_test.cpp](../../tests/fixed_timer_test.cpp) passes **457,701
checks on each of the x64 and x86 architectures**, in Release, MSVC
19.44.35229.0, SDK 10.0.26100.0, `/std:c++latest /W4 /WX /permissive-
/utf-8 /fp:precise`. Private builds under
`<private-evidence>/reprise-2026-10-30/fixed-timer-cpp/build` and
`build-x86`.
The comparison test also links the `LegacyPhysicsTimer` implementation.

The main integration also passes **28/28 CTest x64**, including the new
`fixed_physics_timer_owner` test:

```powershell
cmake --build build --config Release --target fixed_physics_timer_test
ctest --test-dir build -C Release -R fixed_physics_timer_owner --output-on-failure
```

The bounded oracles compute `ticks*90/F` directly, independently of the
code's modular conversion. The scenarios cover:

- 120 seconds at 20, 60, 144 and 500 notifications/s, prime QPC frequency
  of 1,000,003: exactly 10,800 steps, with no final remainder;
- 11 ms wake-ups and jitter, repeated QPC values and preserved fraction;
- one hour of lateness: 324,000 steps completed without loss, budget of
  seven;
- debt/fraction abandoned by pause or stop, resume without pause catch-up,
  ED priority, zero-delta EB/EC calls, old tokens and a reused native ID;
- zero QPF, zero budget, failing or regressing QPC, 64-bit arithmetic
  limits;
- an integrator that has already produced an effect and then thrown,
  refusal of any repetition;
- reentrancy and real concurrency, with deterministic latches around the
  integrator, busy stop then retry and closing of admission.

Over the same 909 wake-ups of 11 ms, that is 9.999 seconds, the legacy
fixed-delta path emits 909 calls and about 10.1 seconds of deltas; the
legacy measured-delta path emits 909 calls totalling about 9.999 seconds.
The new schedule completes 899 steps and keeps `0.91` of a step as its
fraction. This comparison provides the three owners with the same non-zero
QPC base, already initialized. These numbers verify an intended scheduling
difference, without validating a vehicle or Havok.

No game, target process or hook was launched or modified for these tests.
Native admission, the generation bridge, the service transitions and the
measurement in a loaded world remain separate work before a real hookup.
