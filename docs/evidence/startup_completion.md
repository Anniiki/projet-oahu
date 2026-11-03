# Derived startup and service registration

`complete_startup_97c8f0` reconstructs the entire observed caller from
`0x97C8F0` through its two returns at `0x97CDA6` and `0x97CDAB`.
`DerivedStartupCompletion` connects it to `StartupLifecycleBinding`, using
the same controller, service registry and initialization helpers. It does
not provide substitute successful service implementations.

## Primary evidence

Reference: official PC MC 1.66 A, 16,797,696 bytes, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The retained local Rea lot is
`reprise-2026-11-03/controller-startup-host-v1/`.

The first session contains 314 requests, including a bounded walk of 302
existing Ghidra instructions. Both return paths are reached and no jump
leaves the declared range or remains unresolved. Calls are recorded but
their return behavior is not proved by that intraprocedural closure.
Ghidra has no defined procedure at `97C830`; that request was retained as
an error, with its separate raw byte window. No function was created.

A second session resolves the static startup-gate data chain and reads the
five direct callees around derived registration. It contains 11 requests,
including an unsuccessful procedure request at `504770`. Both sessions
exported evidence and closed successfully; their nonzero client exits
reflect those retained request errors. The official executable's digest
is unchanged. No retail process was launched for this analysis.

The 38 registration sites and object identities are independently retained
in `tests/fixtures/startup_completion_services.txt`, extracted from the
primary instruction records rather than the C++ service table. Earlier
secondary work is corroborating context, not the source of these new
instruction observations.

## Loading path and live state

When byte `11405E4` is nonzero and preferred mode `owner+E8` is not 19,
the caller writes `FA9CD4 = 2` and requests mode 18. It captures the
function pointer at `9EC36C` once, before both path copies. Each path buffer
is cleared as 260 bytes; the captured function receives a count of 259.
The first source identity is `F0B3BC`, the second `F0B398`.

Each successful copy is followed by `F995A8 = 1` and a pending-mode store
at `F995A4`: 18 for the first scene, 24 for the second. The first scene
also requests mode 18 again after its copy, with the current guard and
current mode. This repeated request is observable if the copy changes the
controller state. The formatter call uses `F0B3AC`, `F073C0`, `F0B3B4`, 0.

Each scene allocation reloads the `1699104` allocator through its port and
requests `(0x5C50, 0, 0, 0)`. The result goes to constructor `71EB80`.
The returned scene's field `+2C` is set to 0 or `0x30`, then `97F260` runs,
followed by the scene's virtual slot 0 with argument 1. Only after the
second scene does the caller request mode 24. A blocked mode change does
not skip the surrounding work. Callbacks can change the guard, mode,
globals and allocator between operations.

The alternate path requests the preferred mode, then calls `5066E0` with
receiver identity `FAC4F0` and argument 1. DWORD result 4 returns native
false immediately. Earlier state changes remain; the common path's reset
of `FA9CD4` has not yet occurred.

## Registration and composition

The common path clears `FA9CD4`, then calls `9B0190` with receiver `FA4068`,
`9A4930` with receiver `10E80C0`, and `98E090` with argument `1140360`.
It registers 36 services through the real `register_mode_subsystem`, calls
`98DBB0` and `98DA20` with that same argument, then registers two more.
Native AL false stops after the failed member's end helper and count
increment. Modern refusal and invalid registry remain distinct outcomes.
All prior effects remain available for later shutdown.

After the last successful registration, the live pointer `143F520` is
copied to `FAA5FC`; null is a valid copied value. This is not an additional
dereference or an inferred successful world load. Together with the base
13 services, the normal path contains 51 registered services before its
forward pass. The binding's completion callback receives the same
lifecycle object, so those 38 additional members use the existing
`9B40C0` / `9B4060` helpers and registry instead of a second implementation.

## Explicit boundaries

The static gate chain is `F46590 -> F46594 -> vtable 9F0408`, whose slot
`+4` is `504820`. The only listed instruction at that candidate is a
nonordinary call outside the image. No target is followed and no gate
meaning or implementation is inferred. `DerivedStartupCompletion` remains
abstract because `startup_gate()` is still pure virtual. Test gate values
are explicitly owned witnesses and are not product defaults.

The three [configuration callers](startup_config_drivers.md) now compose
the existing registry, section parser, argument lookup and teardown through
`StartupConfigurationBinding`. Their underlying I/O and format calls remain
explicit ports. The loading constructor, scene loop, virtual destructor,
formatter, display call and concrete service bodies still need implementations.
Their ports have no successful default body.
Unknown calls must be supplied explicitly; a modern refusal stops the
driver rather than fabricating a subsequent native return.

A null or undersized allocation and a null constructor result are refused
before unsafe access. The native instead reaches a null `+2C` store or
unbounded constructor access; these refusals are deliberate modern
boundaries and preserve the preceding writes. This component does not
bypass activation or protection and establishes neither gameplay nor
multiplayer or two-client co-op.

## Validation scope

Focused C++ scenarios cover the primary service sequence, native false
versus modern refusal at every member, display result 4, all observed mode
guard cases, captured copy dispatch, callback mutation between scenes,
allocation and constructor failures, callee refusals, configuration order
and the live final pointer copy. A separate composition case executes the
actual controller, base binding, configuration driver, completion driver
and both member helpers through all 51 registrations and the forward pass.
Only unresolved callees and service bodies are owned witnesses.

Tests use x64-owned state with the game guard, BelowNormal priority and
at most two compiler jobs. They do not execute retail code or open saves.
Eight focused suites pass, with 9,639 checks and zero failures. Three
defective temporary copies are rejected by the tests: omitting the second
mode-18 request, reloading the copy dispatch for the second scene, and
moving the two configuration calls past the wrong service. No canonical
source was modified to inject these defects.
