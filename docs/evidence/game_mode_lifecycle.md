# Game-mode lifecycle and phase identities

This C++ library reconstructs the demonstrated orchestration of the official
game-mode controller. It is not a phase implementation, world loader, or proof
that the reported offline new-game hang is fixed.

## Primary evidence

Reference: PC MC 1.66 A, SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
One local Rea/Ghidra 12.2 session completed 66 requests without tool errors,
then exported and closed. Evidence is retained under the local
`reprise-2026-11-02/game-mode-lifecycle-v1` directory. Five requests in its
final inbox file arrived after idle closure and were not executed.

| Entry | Reconstructed contract |
| --- | --- |
| `0x9B4530` | Guarded mode request; previous changes only for a different mode. |
| `0x97CDB0` | Test the entire startup-gate DWORD before entering base initialization. |
| `0x9B4E40` | Reset count, initialize 13 fixed services in order, configure after the twelfth, invoke derived completion, then run a forward pass. |
| `0x9B4900` | Store the member, set its status to -1, call begin/init/end, then increment count even after a false init result. Count and slot are reloaded around calls. |
| `0x9B45D0` | Forward pass with live count and live slot reads. |
| `0x9B46C0` | Reverse pass with an initial count bound, then a forward pass with live count. |
| `0x9B4560` | Decrement before each shutdown callback, drain the live count, then decrement zero to `UINT32_MAX`. |

The startup service identities, in order, are `FA3FD0`, `10E80C0`, `FA477C`,
`FA3F90`, `1140360`, `1140A00`, `FA4744`, `FA3FF0`, `FA4720`, `FA4320`,
`114B0C0`, `FA4760`, `FA4068`. Configuration receives the controller and the
fifth service. These addresses identify source objects; the C++ implementation
stores owned references and never converts them into callable retail pointers.

## Phase catalogue

The 30 cases come from `0x97CDE0`, its jump table at `0x97D630`, and their phase
labels. Case 3 and values outside 1..30 use the common tail without constructing
a phase. Returning no descriptor does not mean that this tail does nothing.

The 29 phase identities include free ride, map, shop, mission, garage, club,
lobby, login, video bumper and online replay. Four short labels (MAP, SHOP,
CLUB, JAIL) were absent from Ghidra's declared strings; they were checked in
the unchanged PE file at their referenced addresses instead. The retained
test fixture records case numbers, branch addresses and native labels.

Mode 18 decimal is VIDEO BUMPER; mode 24 (`0x18`) is LOGIN. The latter label
does not establish a network connection. No constructor, phase body, common
tail or gameplay service is implemented by this catalogue.

## Ownership, callbacks and limits

The runtime owns subsystem references in x64-compatible storage. Only the
current callback is pinned; later slots and counts remain live. This preserves
reentrant append, replacement and count reduction during the demonstrated
passes. Shutdown retains slots and records the native closed-count sentinel;
initialization can reset the count for another start.

The API is for the owning thread. Callback progress and service behavior remain
their implementers' responsibility. Null, missing, closed or inconsistent
indexed registry accesses return `invalid_registry`; native invalid-pointer
behavior is not reproduced. After the last initialization helper returns,
registration unconditionally increments the DWORD count, including wrap to
zero. It returns the initialization result without another indexed access.
Any subsequent operation validates its own accesses. Initialization failure
preserves its partial registry.

The gate, service resolution, initialization helpers `0x9B40C0`/`0x9B4060`,
configuration `0x98E960`, and derived completion are explicit interfaces.
The [startup lifecycle binding](startup_lifecycle_binding.md) now supplies
the reconstructed helper and configuration bodies. Derived completion's
[caller driver](startup_completion.md) uses the same registry for another
38 services. The gate and unresolved callees still need implementations.
A callback may change a mode:
the lack of a direct mode write in `0x9B46C0` does not exclude such an effect.

`game_mode_lifecycle` checks primary phase identities, unsigned mode values,
nonzero guard bytes, startup order and failure at every service, callback
identity, live mutations, shutdown order and the terminal count. This is
component validation, without running or modifying the commercial game.

The initial MSVC C++latest x64 run passed 286 checks. Five isolated source
mutations were then detected by failed checks: omitting failed members from
the count, reversing the wrong way, caching the forward count, testing only
the startup gate's low byte, and confusing decimal 18 with the login phase.

An independent review found two additional divergences when a simulated
end helper changes the count. Their reachability in the native game is not
established. The same adverse witness failed on the initial port; the final
increment now follows `0x9B494E` without the unnecessary post-helper refusal.

A subsequent primary session read `0x9B40C0` and `0x9B4060`: they test the
global controller's byte `+0xE4`, allocate using its DWORD `+0xEC`, store the
result in member `+0x04` or `+0x08`, then fill bytes with `0xBD` or `0xAD`.
Neither consumes the incoming ESI/ECX register values. They still require a
global-controller/allocator context when implemented; the member argument
alone is not all their state. Allocation can have indirect effects that the
listings do not resolve. These helpers remain ports in this library.

Initialization now distinguishes a native AL=false return from a modern
refusal before a native return. Only the former reaches the end helper and
count increment. The same typed control flow propagates helper/configuration
refusals; completed native calls retain their original ordering and count rules.
