# State dispatcher `407FB0` and empty callback `407F80`

## Result and scope

`LegacyStateMachine` reconstructs the decisions observed in `00407FB0`
with an injectable C++ handler. It preserves the initial event, the order of
the `2/1` pair, the rereads of the fields between callbacks and the limit of
ten transitions per invocation. The empty callback `00407F80` is represented
by a function and a **void** handler; no boolean success is invented.

The delivered files are `include/tdur/game/legacy_state_machine.hpp`,
`src/game/legacy_state_machine.cpp` and their standalone test. They contain
no hook, global clock, native call, process access or dependency on the
OpenTDU engine. This component is not wired into the game; it does not by
itself restore driving decisions or multiplayer.

## Primary source and corroboration

x86 VA, preferred base `00400000`:

- Official PC MC 1.66 A: SHA-256
  `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
- Secondary Platinum/Paradise: SHA-256
  `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.

The private Rea exports already available are reused from
`reprise-2026-10-30/rea/paradise-ai-time/`:

| Export | Evidence |
| --- | --- |
| `152-resolve_containing_procedure.json` | Body `407FB0..407FF2`, 67 contiguous bytes |
| `153-procedure_assembly.json` | Instructions and order of reads/writes; evidence `ev_58365819b55f6a62c80ee7a609b82b9466ba22fa43397716a4b01c43527f34d2` |
| `263-read_bytes.json` | `407F80/3 = C2 08 00`, evidence `ev_d1cb232edb50dad23fc3bb3334d3eec3db7ffe85477bf2e9e1e617c5949f1e89` |
| `disk-comparisons.json` | These two bodies differ in the official binary on disk and are readable in the secondary |

The closed official trials 18 (`20261030T120132-19748`) and 19
(`20261030T121220-31820`) then captured the same bodies in memory:

| Complete range | Memory SHA-256, identical to the secondary in both trials |
| --- | --- |
| `407FB0/67` | `3746f26cc005f8d8338e9d06254cc9489d71dfc008ec69f396b606d0c06cc28c` |
| `407F80/3` | `1d10894625976bca9d8906f3ecf1e766b94e9219ec3bf1c30e02feea5e18e45a` |

Reports reviewed:
`<isolated-copies>/slot0/<run>/code-helpers/comparisons.json`,
51 ranges and 12,350 bytes per trial. The equality covers the complete
bodies, with no edits to those ranges; it does not prove their execution. No
IAT read or protection transformation is needed for this reconstruction. The
pseudocode serves as a lead, the assembly establishes the contract. The
original Rea session was exported then closed; no new session is opened for
this module.

## Observed ABI and layout

`407FB0` uses an internal x86 convention:

- **ESI** designates the state machine sub-object.
- **EDX** supplies the initial event, 32 bits wide, with no observed
  restriction.
- The dispatcher preserves EDI, uses it for its local budget and ends with
  `RET` at `407FF2`, with no arguments to clean up on the stack.
- Its first virtual slot is called with **ECX = this sub-object** and two
  DWORDs on the stack: event, then state. The callback cleans up eight bytes.
- The callback result is never tested. AL is reloaded from the pending flag
  after the call; the dispatcher's return register is not an established
  boolean success.

| Native offset | Size read/written | Established role |
| --- | --- | --- |
| `+0` | x86 pointer, 4 bytes | Callback table, first slot used |
| `+4` | DWORD | Current state, copied as argument |
| `+8` | DWORD | Requested destination, reread after callback `2` |
| `+C` | BYTE | Pending transition: any non-zero byte is active |

This table does not fix the complete size of the sub-object or its
following fields. `LegacyStateFields` is a portable logical representation,
with no native pointers and no guarantee of a compatible binary layout.
States use `uint32_t` to preserve all bits: zero, `7FFFFFFF`, `80000000`
and `FFFFFFFF` are not special sentinels in this helper. No increment or
identifier validity check appears in the native body.

The public names remain `LegacyStateEvent::raw_0`, `raw_1`, `raw_2`.
Callers that were studied pass zero; the dispatcher emits the pair two,
then one. The terms "exit/entry" below designate this pair, without
attributing any additional domain meaning to the arbitrary events.

`407F80` is only `RET 8`: no argument is interpreted there, no field is
modified, no return register is initialized. The C++ `legacy_state_noop`
adapts this behavior to the chosen void interface. The native body is also
shared by other vtables; this does not prove that both of its DWORDs are
event/state identifiers everywhere.

## Precise order of effects

| Instructions | Retained effect |
| --- | --- |
| `407FB0..407FBA` | Snapshot of the current state and of the binding; initial event delivered once, even if a transition is already requested |
| `407FBC..407FC9` | Pending test after this callback, initial budget ten; decrement before each pair, return when the native counter becomes negative |
| `407FCB..407FD9` | New read of the current state and of the binding; pending reset to zero **before** callback `raw_2` |
| `407FDB..407FE8` | Destination and binding reread **after** `raw_2`; destination copied to current **before** callback `raw_1` |
| `407FEA..407FEF` | New pending test after `raw_1`, then next pair if the budget allows it |

These rereads imply in particular:

- A mutation of the current state during the initial event changes the
  state passed to `raw_2`. The argument already received by the first
  callback remains a value snapshot.
- A destination write during `raw_2` immediately changes the entered state.
  A write of the current state during this callback is then replaced by
  this destination, following the native order.
- Clearing pending in `raw_2` does not cancel the pair already started:
  `raw_1` is still called.
- A **new request** during `raw_2` sets pending. Its destination is used
  immediately, but pending is not consumed at that point. Without
  cancellation by `raw_1`, another pair follows, possibly towards the same
  state. No "current == destination" optimization is performed.
- A request during `raw_1` is kept for the next pair. A direct mutation of
  the current state in this callback is not rewritten after its return.
  Changing only the destination does not set pending.
- The binding is reselected for each callback. A replacement in `raw_2`
  takes effect from the `raw_1` of the same pair.

The C++ therefore exposes the three fields independently.
`request(destination)` is only a convenience: destination write, then
pending=1. Non-canonical pending bytes are not normalized on entry or on
return; only the reset to zero before `raw_2` is imposed by the dispatcher.

The native counter allows ten pairs, hence at most 21 callbacks directly
emitted by a pass that returns normally. If the tenth entry still requests a
transition, the fields remain intact and the request waits for a **new
explicit call**. If it requests nothing, ten completed pairs do not by
themselves constitute an overrun.

`LegacyStateDispatchResult` is an observation **new to the C++**: number of
pairs fully completed in this invocation, final pending byte and indication
of budget reached with a remaining request. It does not reproduce an
identified native result. Reentrant callbacks are not added to the counter
of the calling frame.

## Exceptions, ownership and reentrancy

The machine must stay alive during each callback and all nested frames.
Copying and moving it are forbidden. The host serializes access to the
fields, to the binding and to `dispatch`: no synchronization or thread
safety is claimed here.

Each callback keeps a local copy of its handler's `shared_ptr`. A
replacement therefore cannot destroy the handler during its own method; its
last reference is released after the return or during exception unwinding.
This choice **strengthens lifetime guarantees** compared with the plain
native table and does not claim to reconstruct its allocation. Handler
destructors must remain exception-free and must not mutate or dispatch the
machine: they do not constitute additional native events. The host must not
destroy the machine from a callback. Handler ownership graphs belong to the
calling C++, not to the historical dispatcher.

The native body has no reentrancy guard. The module preserves synchronous
reentrancy: a callback may call `dispatch` on the same machine, with its own
local budget of ten pairs. The outer pass then resumes on the resulting
fields, without restoring an old snapshot. The limit of ten is therefore not
a global bound on recursion or on the work of a whole callback tree; the host
remains responsible for that contract.

The behavior of C++ exceptions is not attested by the native bytes. The
explicit choice of the port is to propagate the exception and keep the
writes already made, including a binding change:

| Throwing callback | Dispatcher writes already performed |
| --- | --- |
| Initial event | No field write by the dispatcher; the handler's mutations remain present |
| `raw_2` | Pending has already been cleared; no automatic publication of the destination and no call to `raw_1` |
| `raw_1` | The new current state has already been published; no restoration of the old state |

No success result is returned on exception, no partial pair is replayed, no
continuation is scheduled. A later call is an explicit decision of the host
and begins with its new initial event. A null handler is refused with
`invalid_argument` before the binding is replaced; this validation is a C++
contract for valid inputs, not reconstructed native error handling.

## Standalone validation

MSVC 19.44.35229.0, Release, `/std:c++latest /W4 /WX /permissive- /utf-8`,
with x64 and Win32 compilation recorded in the private evidence: **1,682 checks
passed on each of the two architectures**. The expected traces are written
separately from the algorithm; a family of linear chains of zero to 35
states verifies progression and bounded resumptions.

The scenarios also cover extreme states, pending bytes `80/FF`,
self-transitions, cancellation, independent changes of the three fields at
each phase, destinations modified during `raw_2`, bindings changed at each
callback, lifetime preservation, exceptions at the three phases with and
without mutations, reentrancy and the refusal of a null binding. The noop is
tested directly and then through a dispatcher transition.

Binaries, logs and hashes remain private under
`reprise-2026-10-30/legacy-state-machine-cpp/`. The format of the three C++
files is checked with clang-format 19.1.5, four spaces and 80 columns. After
integrating the `tdur_legacy_state_machine` library and the
`original_state_machine_dispatch` test into the main CMake, the **29
Release/x64 CTest cases pass**. An independent review of the access order,
mutations and lifetimes found no defect. No game launch, native handler call
or NPC integration is included in these results.
