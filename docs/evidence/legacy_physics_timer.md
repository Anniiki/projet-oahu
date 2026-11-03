# Legacy physics callback `0081EE50`

## Scope and source

[legacy_timer.hpp](../../include/tdur/physics/legacy_timer.hpp) and
[legacy_timer.cpp](../../src/physics/legacy_timer.cpp) reconstruct the
decisions of the callback with an injected clock and integrator. They create
no WinMM timer, access no process and do not implement Havok. The official
client is neither modified nor connected to this module.

The primary reference is the official PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The secondary Platinum/Paradise build carries the SHA
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The **complete 596 bytes**, `0081EE50..0081F0A3`, have the same SHA in both
files on disk:
`027f8d2b7fdcef0d4fc9dc375366f2b8020ca2746380cd5924e0e2586a2a6374`.

Evidence is kept under
`<private-evidence>/reprise-2026-10-30/rea/paradise-focus-follow/`:
`physics-periodic-callback.asm.txt`, `physics-worker-prefix.asm.txt`,
`targeted-disk-comparisons.json`, Rea outputs, bundle and closed snapshot.
Ghidra 12.2 does not recognize this callback as an attributed function; the
Rea instructions and reads are supplemented by the MSVC `dumpbin`
14.44.35229.0 static disassembly. No pseudocode is taken as a source.

The closed official trial `20261030T083003-25076` corroborated the complete
body in memory through `81EE50/512` then `81F050/84`.
[retail_focus_timing.md](retail_focus_timing.md) details the measurement and
its limits. Equality of the code does not prove that it was executed in the
menu. Closed trial 14 (`20261030T102129-30308`) records `F8A2E8=1` (measured
delta), `168D6B8=0.0111111114` (float period 1/90), `EB=EC=0`, `ED=1`,
`last_callback_ms=0` and no integrator entry in its 26 menu samples. No real
physics cadence or driving is measured there.

## Typed interface

`LegacyPhysicsTimer` owns the scalars and the 100 reconstructed costs.
`tick(clock, integrator, flags)` returns its decision, the MS delta modulo
32 bits, the elapsed QPC time, the delta passed to the integrator call, if
one is made, and the initialization indicator. The four flags keep their raw
address names. They are supplied as a consistent snapshot for this call;
successive reads of the native globals were not such an atomic snapshot.

`LegacyTimerClock` provides `milliseconds()` and `qpc_ticks()`;
`LegacyTimerIntegrator` receives the two observed floats. The second is
always **+0.0**, without assigning it an unestablished gameplay meaning.
A non-zero QPC frequency is required by the C++ adapter; the constructor
rejects zero. This refusal is not presented as a branch of the native
callback, which does not check QPC success at this point. The period remains
a raw float, including zero and NaN.

The atomic guard refuses reentrancy or a concurrent call before any clock
read. It strengthens the serialization of the native boolean `10E7C08`; it
does not claim to reproduce the races of its non-interlocked test/write.
`try_snapshot()` returns empty during a call; otherwise it copies the state
under that same guard. A tick concurrent with this short copy may therefore
be refused as busy: query between calls or once stopped if the observation must
not influence a future cadence. Exceptions from an adapter release the
guard but do not undo effects already produced. The object must remain alive
until all its calls have exited, whether they return or throw.

## Decision order and effects

| Instructions | Reconstructed behavior |
| --- | --- |
| `81EE50..81EE73` | Refusal if busy; guard then first MS read |
| `81EE75..81EEA1` | Replacement of `10E7A5C`, periodicity diagnostic |
| `81EEA9..81EED6` | QPC read; if the last QPC is zero, it becomes `QPC + deltaMS` |
| `81EEDB..81EF42` | 64-bit modular subtraction, **unsigned uint64** conversion of the difference and of QPF, quotient stored as float32 |
| `81EF46..81EF5C` | Refusal only if `period / 2 > elapsed` |
| `81EF62..81EF70` | Last QPC replaced by the QPC actually read; cost start MS |
| `81EF72..81EFA9` | Integration selection according to the flags below |
| `81EFAE..81EFD9` | End MS, cost `uint32(end - start + 1) * 0.001f` |
| `81EFE0..81F081` | Shift of the 99 previous costs, addition of the new one, average over 100 |
| `81F055..81F08F` | 64-bit modular increment of the admissible-callback counter |
| `81F094..81F0A3` | Guard release and return |

The final increment is not a counter of positive integrations: it takes
place even if `ED` suppresses the integration or if `EB/EC` force zero. A
refusal at the half step still updates the diagnostic and the last MS; it
does not profile and does not replace the last QPC, except for its
initialization.

| Condition, in this order | Integrator call |
| --- | --- |
| `raw_129beed != 0` | None |
| Otherwise `raw_129beeb != 0` or `raw_129beec != 0` | `(0, 0)` |
| Otherwise `raw_f8a2e8 != 0` | `(measured elapsed, 0)` |
| Otherwise | `(period 168D6B8, 0)` |

The values are tested as non-zero, not as equal to one. No "menu",
"manual pause" or "focus" meaning is invented for them.
[retail_simulation_dispatch.md](retail_simulation_dispatch.md) describes the
known writers and the corresponding scene paths.

The assembly branch is `JA`: an equality accepts the callback and an
unordered comparison, notably with a NaN period, does not skip it. The code
therefore keeps `half_period > elapsed`, without turning it into an inverted
test of the form `!(elapsed >= half_period)`.

## Preserved numerical particularities

When the last QPC is still zero, it is initialized by adding **raw
milliseconds to the ticks**, without unit conversion or subtraction. For
example, an MS delta of 11 then produces an unsigned difference of
`2^64 - 11`. The reconstruction preserves this unusual behavior. After an
admissible callback, the last QPC equals the real QPC; no time debt or period
remainder is kept. A second call at the same QPC is too early for an ordinary
positive period, even after a long interval.

The native diagnostic equals `100 - float(int32(abs_bits32(deltaMS - 10)))`.
The 32-bit operations are preserved without undefined signed overflow:
`0x80000000` remains that pattern after the absolute value, then `CVTSI2SS`
reads it as `INT_MIN`. A usual C++ absolute value or an unsigned conversion
of the result would change this edge case.

Constants read from the PE sections of the original, without execution:

| VA | IEEE-754 word | Value |
| --- | --- | --- |
| `F41708` | `42C80000` | 100 |
| `F41674` | `3F000000` | 0.5 |
| `F41670` | `4F800000` | `2^32`, conversion correction for the unsigned cost |
| `F4167C` | `3A83126F` | `0.0010000000474974513` |
| `F416D4` | `3C23D70A` | `0.009999999776482582` |

The array `10E7A68..10E7BF4` holds 100 floats. The first cost is not
averaged over a single sample: the 99 initial zero slots remain in the
average. The code sums the 99 shifted previous costs in their order, then
adds the new one and multiplies by the float constant `0.01f`. The MS
subtractions, the `+1` bias and the 64-bit counter wrap within their
respective widths.

The native code uses x87 for the QPC quotient and the cost, scalar SSE for
the history sums. The port uses `long double` before float conversion for
the former and ordered float additions for the latter. On **MSVC x64,
`long double` has only a 53-bit significand**; the effective x87 precision
and rounding depend on the native control word, which is not captured here.
The port therefore does not guarantee bit identity for all large counters,
double-rounding cases or half-step boundaries. The tests use exact binary ratios
for the boundaries and independent integer vectors for the wraparounds. They
run without fast-math, under the normal floating-point environment. No FPU
control setting of the game is modified.

The legacy WinMM worker requests `1000 / frequency` in whole milliseconds:
90 gives 11 ms. This module does not replace that cadence with an exact
90 Hz accumulator. A single physics owner and a separate stop/drain
procedure are required before any replacement of the worker.

## Validation performed

[legacy_timer_test.cpp](../../tests/legacy_timer_test.cpp) passes **2 002
checks**, with MSVC 19.44.35229.0 x64 Release, `/std:c++latest /W4 /WX
/permissive- /utf-8 /fp:precise`, in the private build
`<private-evidence>/reprise-2026-10-30/legacy-timer-cpp/build`.

The cases cover the strict threshold and its equality, the order of reads,
the priority flags, the measured/fixed/zero deltas, the skip without
integration, the cost bias, the 100 slots and their average, initialization
and the MS/QPC/counter wraparounds, `INT_MIN`, NaN and signed zero,
reentrancy, real concurrency between threads and release after exception.

An independent sequence of 909 events spaced 11 ms apart represents 9.999
seconds of clock time: the fixed path integrates about 10.1 seconds, the
measured path about 9.999. A catch-up attempt at the same QPC produces no
additional step. These results validate the standalone model; they
demonstrate neither Havok integration nor an active fix in the game.
