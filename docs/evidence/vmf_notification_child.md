# Child notification: 89DD60

`VmfNotificationChildContext` reconstructs the control of a child
notification in owned C++, independent of the x86 ABI. It complements the
[state computation](vmf_notification_state.md). The offset-based names keep
the provenance of the fields without assigning them an unproven domain unit.

## Order of effects

A zero `active_08` byte terminates immediately with 0. Otherwise, all the bits
of `mask_0c` must be present in the fixed mode of the call. The C++ virtual
evaluation service is actually called with a NULL output argument. Its DWORD
result is then compared, unsigned, to `limit_18`, read after the callback.
The result of this comparison is written to `active_08` before the reads of
the global flag and of the identifiers.

If the computed byte equals 1 and the global 16A2D2F equals 0, the branch
joins the final read of the active byte. Otherwise, the low30/high34 pair
selects what follows. The two sentinels are exactly `FFFFFFFF/FFFFFFFF` and
`00517180/00000000`: a single matching half is not enough.

With a sentinel, a non-zero input byte skips 89DB30; otherwise this service
is called. The branch then rereads `active_08` and returns 1 only if it
equals 0. The other identifiers call 89E330, then 89BF80 if the input byte
equals 0. This path always returns 0, without a final read of the byte.
The return register of the sub-services is not used by 89DD60.

## Services and continuations

The evaluator and the effects are mandatory C++ interfaces: their
implementations must execute the real effects. The [89E330 dispatch](vmf_notification_dispatch.md)
is wired through its concrete frame, like the effects
[89BF80](vmf_notification_effect_bf80.md), [89DB30](vmf_notification_effect_db30.md)
and the [89E060 parent](vmf_notification_parent.md). The
[shared pool](vmf_notification_effect_pool.md) routes the three effects;
the complete hosts remain to be wired. An unknown binding
requests the service; an explicitly NULL binding produces an error.
No missing service is converted into a success.

The context retains the selected evaluator, its result, the comparison,
the captured low-ID and the effects module for this invocation. A resumption
replays no finished callback and no store. Evaluation stays synchronous;
the effects have the modern resumption protocol described below.
An error preserves their prefix and makes the context terminal. Global data
is left for the concrete implementations to read at the required time.

References and modules must stay valid without relocation, with serialised
access. A move invalidates the source. Reentering or moving the same
context during `run` is forbidden; a recursive native call requires another
context. Budgets and waits are explicit modern boundaries.

## Resumption of nested effects

`VmfNotificationChildEffects::begin_effect` by default calls the real
synchronous `invoke` exactly once. A null frame pointer means **effect
finished**, never an unknown service. An implementation that waits for a binding
must provide a live `VmfNotificationChildEffectFrame` reserved for this
invocation. The base implementation without a concrete effect fails explicitly.

The context captures the frame and the module. It then calls `run(1)` on
that same frame, without calling `begin_effect` or the evaluator again. A
`pending` return hands control back immediately with `effect_pending`, even if
no internal progress took place and the root budget remains high. A zero
root budget calls no service. The counter of finished effects increases
only after the real completion; `89BF80` therefore does not start during a
wait on `89E330`. The final reread after `89DB30` stays late.

The frame is detached before the virtual `release()` call, executed exactly
once at the end, on error, on abandonment or on replacement by move.
This release returns the slot to its owner; it executes no game
effect and does not cancel the prefix already produced. The context performs
no ordinary allocation; its modules and their pools may perform some.

**Modules, frames, arguments and objects must outlive the context**,
including its destruction while pending. Declare these dependencies before it or
destroy its continuation explicitly before freeing them. The module must
guarantee an exclusive reservation and stable addresses; the protocol
does not synchronise its pool. Accesses remain serialised by the caller.

A move transfers the single reservation and invalidates the source. During
`run` or the replacement of a frame, reentrant calls are refused.
A move construction from an active context yields an
invalid destination without modifying the source; an assignment involving
an active context leaves both objects unchanged. Destruction also protects
the release against an accidental resumption of the same context.

This adaptation concerns the modern waits of the reconstruction. It
does not claim that the original x86 program was asynchronous. The dispatch
`89E330`, the two other effects and the parent `89E060` are provided in
separate batches; their targeted compositions do not yet prove a playable
commercial menu or a co-op session.

## Validation of the 2026-11-01 resumption

**49 targeted suites pass in each of the x64 and Win32 architectures**,
MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`. The 184 existing
checks remain unchanged; 55 new checks cover the order of
effects, the module change during a wait, the errors, the guards,
the budgets, the moves and the single release. These checks are
active with `NDEBUG`, verified in the Release compilation options.

The test uses the real C++ context and declared synthetic evaluators, effects
and frames. It does not replace the tests of the future native bodies. The
58 tracked sources and six sensitive files remain unchanged
during the validation. Private evidence: `vmf-child-continuation-runtime-v1/`.
No new native body or new game trial is claimed.

```powershell
cmake --build build --config Release --target vmf_notification_child_test vmf_notification_child_async_test
ctest --test-dir build -C Release -R '^(vmf_notification_child|vmf_notification_child_continuation)$' --output-on-failure
```

## Validation on 2026-10-31

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: 19 suites pass
in x64 and Win32, including 184 checks of this batch. In particular, they cover
unsigned comparisons, guards, partial sentinels, mutations by
callback, errors without replay, resumptions, move and budget. One composition
actually executes child, sum, level and store on owned objects.
The measured absence of ordinary allocations concerns these fixtures only.

```powershell
cmake --build build --config Release --target vmf_notification_child_test
ctest --test-dir build -C Release -R '^vmf_notification_child$' --output-on-failure
```

PE, Rea and DUMPBIN agree on the 128 bytes of the 89DD60 body with
the official MC1.66A primary on disk, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Private evidence: `reprise-2026-10-31/vmf-notification-child-runtime-v1/`.
Five frozen sources and six unchanged sensitive files are verified.
An independent review reported no concrete defect on four sources/31307B;
a second reviewer cross-checked the hashes, the conditional oracles and its
own tests.

No game trial was performed for this batch. The 89E060 parent, the complete
effects and the wiring to the menus remain to be finished; this result
validates neither playable commercial menus, nor native D3D12 rendering,
nor co-op.
