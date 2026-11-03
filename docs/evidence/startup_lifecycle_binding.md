# Startup lifecycle composition

`StartupLifecycleBinding` connects the reconstructed mode controller to the
existing initialization helpers and configuration driver. Its reference is
official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The contracts come from the retained primary listings of `0x9B4900`,
`0x9B4E40`, `0x9B40C0`, `0x9B4060` and `0x98E960`, already documented in
[mode lifecycle](game_mode_lifecycle.md),
[startup services](startup_services.md) and
[configuration startup](config_driver_98e960.md).

## Control flow

The controller uses three initialization outcomes: a native true return,
a native false return, and a modern refusal before a native return.
The last is not an AL value. A false return still invokes the final helper
and increments the service count. A refusal does neither. This distinction
also applies to the two member helpers and the configuration call.

Previously, `Service9b1df0` collapsed every non-completed driver result into
false. When the owned driver refused the null dereference at `0x9B1E3B`, the
controller consequently fabricated the later `0x9B4060` call and increment.
The typed result now preserves the distinction from the native false return
after `0x610840`/`0x610900`.

The binding forwards each live service lookup, then calls the actual
`initialization_helper_9b40c0` and `initialization_helper_9b4060`. Their global
context and allocator remain live references; their existing bounded fill and
ownership rules are reused. Between service 12 and service 13, the same saved
fifth service and controller are passed to `run_configuration_driver_98e960`.
A configuration refusal preserves its partial effects and prevents service 13,
derived completion and the forward pass.

The final DWORD increment remains unconditional after a returned end helper,
including wrap to zero. The binding does not undo callback mutations or infer
that missing configuration blocks constitute a native failure.

## Ownership and remaining implementations

The caller owns the service table, helper context, allocator, configuration
state and its ports. There is no second registry or numeric duplicate of
object identity. Per-call diagnostic outcomes describe the last actual call;
they are not a declaration that an entire startup attempt completed.

The [derived completion caller](startup_completion.md) now registers the
additional 38 services through the same binding. Its typed outcome preserves
native false, modern refusal and invalid registry before the forward pass.
The startup gate, concrete service implementations, phase bodies and unresolved
configuration callees still require implementations.
No default successful service or cycle callback is supplied. This composition
does not establish a loaded world, a working Oahu profile or multiplayer.

## Validation

Seven focused x64 CTests pass, with 987 checks and no failures. The composition
witness uses real controller/helper/configuration bodies; unknown services and
callees are explicitly source-owned test witnesses. It checks the complete
13-service ordering, the 375 registration callbacks of the public registry
subset, the exact fifth-service identity, partial effects, live helper
context replacement and native false versus modern refusal. Allocation
failures preserve surrounding owned bytes.

An independent baseline built from the preceding source revision reproduces
the real `Service9b1df0` null-refusal case: three assertions fail because the
old controller reports rejection, invokes the end helper and increments count.
The current test reaches the same refusal and observes neither later effect.
The original successful and native-false cases continue to pass.

All executions use owned C++ data, the project game guard and BelowNormal
priority, with at most two compiler jobs. No game or original save is opened
by these component tests.
