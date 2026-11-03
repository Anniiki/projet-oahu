# Routing commands to the handlers

## Evidence and scope

`VmfFsRouterContext` reconstructs the wrapper `99A6C0..99A6D9` (26 bytes) and
the router `99A350..99A3D1` (130 bytes). The primary reference of the project
remains the official PC MC 1.66 A executable, SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
These two routines are established only in the secondary
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The earlier PE/Rea checks attest their complete bodies, SHA256 respectively
`ef5f71670531f9ccdee5fe8ad5f5fe24c82e564d5b31ae63594fd3ee159fdca3`
and `4e0ba9ebd795874a8330a74e14fb416308ef60762c53cd0641edf6aee2157d38`.

Private evidence: `reprise-2026-10-31/vmf-fscommand-router-runtime-v1/`,
exports `rea/vmf-host-callback-v2/session/13-procedure_assembly.json` and
`17-procedure_assembly.json`, the earlier `exact-byte-validation.json` of the
same batch. The official 144-byte capture of `445CB0` does not corroborate
these routines. This batch required no new access to the game, the process or
the executable.

## Reconstructed order

The wrapper captures the handler `10E7284`, the command and the opaque
userdata once. The x86 addresses are never converted into C++ pointers.
An initial film `manager+E8` known to be NULL terminates without diagnostic
or handler.

The diagnostic branch keeps its short-circuits: byte `1699170`, signed level
`[169919C+30] >= 3`, byte `FAA719`, then non-NULL sink `FA8FD4`.
It calls `402E60` with the live command, level 3, line `0x517` and the
loader's string constants. A non-zero AL triggers the IAT call
`9EC154`; a non-zero EAX reaches `INT3`. The identity of this import is not
assumed. Logger, probe and interrupt are separate mandatory complete
services; their absence suspends the router instead of simulating their effects.

The virtual of the sub-object `manager+F7C` then executes. Its AL is read
only after its real end and before its frame is handed back. Any non-zero
value terminates. A zero AL reloads the film from the same captured handler.
A film that has become NULL produces an explicit modern refusal before the
dangerous native dereference. A receiver `film+AC0` known to be NULL terminates
normally. Otherwise this receiver executes, and its AL result is never queried.

## Owned frames and limits

`VmfFsHandlerStart` distinguishes a synchronous result that has really finished
from a frame to be resumed. Waits replay neither the diagnostic nor the handlers.
Captures survive resumptions; the command windows stay alive and each consumer
reads its prefix up to NUL. Unknown data waits, without becoming zero or NULL.
Errors and abandonment keep the effects already executed.

`VmfFsDefaultCallback` connects this context to the envelope `445CB0`. It
preallocates 1 to 32 slots at construction; this bound is not a player capacity.
The first step of `begin` captures the handler if it is resolved. Each slot
stays reserved during hand-back, and the pool closes before its destruction.
After hand-back the pointer is discarded: no protection against its reuse
after the slot is reassigned is claimed. Serialise all operations of the pool
and of the dependencies, preserve object stability and service affinity.

The bodies of the fifteen global commands `99B7A0`, the film receivers and
the diagnostic remain separate. This router chooses no assumed receiver for
`PC_LOG_IN`, does not pop GetURL and does not advance the VMF cursor.
The commercial player stays at its explicit boundary of missing services.

## Validation

The suite uses synthetic handlers with the real setters of the owned
dictionary. It covers AL 0/1/255, a film replaced or made NULL, captures
between waits, diagnostic, budgets, faults, reentries and two really distinct
frames waiting. The real path `445CB0 → owned callback → router → two
handlers → variable writes` is exercised. A probe on this prepared path
observes zero calls to ordinary `new`; this proves neither a universal absence
of allocation nor the smoothness of the game. The diagnostic fixtures trigger
no native trap. The tests validate no playable commercial menu, authentication
or co-op.
