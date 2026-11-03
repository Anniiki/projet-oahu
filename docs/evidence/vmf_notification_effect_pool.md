# Notification effect routing

`VmfNotificationEffectsPool` provides the shared module used by the children
to borrow the real `89E330`, `89BF80` and `89DB30` frames. It is modern
reconstruction infrastructure, not a newly ported native body.

## Registry and reservations

The constructor copies the configurations and the child-to-metadata registry.
It refuses null configuration pointers, inconsistent identities and
duplicates. Dispatch works without superfluous metadata; the two other
effects require an exact match. Globals, catalogs and services that are not
used remain the responsibility of the selected frame.

The pool preallocates 1 to 32 stable slots, each with its three frames and
its input ports. This resource bound does not describe the number of players.
A completion or an error keeps the reservation until `release()`. Saturation
produces an explicit error, with no late allocation and no fake success.

Each slot protects its own `run` and its hand-back. A callback cannot
destroy its active frame, but it may borrow another free slot. Shutdown
precedes the final hand-back while all slots still exist. Abandonment does
not roll back effects that have already been performed.

## Integration contracts

Construction spans may disappear. The pointed-to objects (inputs, children,
metadata, owners, services and texts) remain alive, stable and serialized
during reservations. The formatters of overlapping invocations must honor
their own exclusivity; the pool does not invent their independence. The
affinities of the callees, in particular the locks held by the parent,
remain applicable.

The pool must not be destroyed during a call or a callback. After hand-back,
discard the frame pointer: it does not protect against reuse of the slot for
a new reservation.

## Validation and limits

**56 suites pass on each of the x64 and Win32 architectures**, including **312
pool checks**, active in Release/NDEBUG with MSVC C++latest. The tests cover
the bounds, the registry copy, the errors, saturation, two pending formatters
and the recursive borrowing of another slot.

The three real routes are executed: the dispatch setter, the lookups and
single ring of BF80, the evaluator/record/queue/double ring of DB30. The real
child path uses the pool, cancels itself, then republishes with the same
objects. The prepared routing probes observe zero ordinary allocations; they
do not guarantee the smoothness of the full game. The shutdown guard during
hand-back is also reviewed statically, without an artificial callback.

```powershell
cmake --build build --config Release --target vmf_notification_effect_pool_test
ctest --test-dir build -C Release -R '^vmf_notification_effect_pool$' --output-on-failure
```

These fixtures are not a game session. The complete hosts and loaders,
FSCommand, the commercial menus and the co-op remain to be connected.
