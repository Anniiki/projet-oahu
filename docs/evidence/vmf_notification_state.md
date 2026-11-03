# State computation and level write: 89BD90, 89C210, 889230

The C++ runtime ports the computation used by the notifications and its
host caller. The fields named by offsets are owned data, with no claim to
reproduce the x86 ABI or to define the domain meaning of their units.

## Sum and thresholds

`VmfNotificationStateContext::sum(owner)` captures the signed count
`count_0c` once. If it is not positive, the sum is 0 without reading the list.
Otherwise it also captures its base once, then reads its slots in sequence.
A NULL slot is skipped. For a non-NULL child, a non-zero `active_08` byte
avoids the read of `weight_14`; if it is 0, the DWORD is added to the sum.
The addition stays on 32 bits: `0xFFFFFFFF + 3` gives 2, without saturation.

`level(owner)` actually performs this sum, then compares it unsigned against
the thresholds in the order 354C, 3548, 3544, 3540, 353C. The first threshold
reached gives 5, 4, 3, 2, 1 respectively; none gives 0. The thresholds are
neither sorted nor all read ahead of time. Even an empty list gives 5 if the
first threshold is 0.

A count or a base modified after their capture does not replace the retained
values. Slots not yet read remain live; a child and the decision taken on its
byte are kept while the context waits for a weight. The weights are not
recomputed when the context then waits for a threshold.

## Host caller and resumption

`VmfNotificationStoreContext` ports 889230: retain the host, actually call
its C++ interface `notification_state()` corresponding to virtual slot +4C,
compute the level of the returned state and write the result into the logical
field `level_f028` of the fixed target argument.

The context retains the selected host and the returned owner. Waits and moves
do not call the host service again. An unknown binding is distinct from a NULL
pointer: a NULL host or a NULL return would be dereferenced by the native code;
here they produce an explicit error. The counter `completed_host_calls` counts
successful returns, including a NULL return that is then refused; an error
return does not increment it.

The nested computation shares the budget. If it ends at the limit, the write
waits for the next resumption. The steps of the computation already completed
remain counted even after a fault; the effects of the callback are kept.
A fault forbids replay. Reusing the same context during its own `run` is
refused; independent calls use distinct contexts.

The target/owner references and the storage objects must remain alive and
must not be moved, with serialised access. Bindings, budgets and bounds
rejections are modern boundaries, without any native atomicity guarantee. A
list base that is too short is refused at the access concerned, after the
partial sum already computed. No native boolean return is invented for 889230.

## Validation (2026-10-31)

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`, x64 and Win32:
18 targeted suites pass, including 239 checks of this service. Covered cases:
32-bit overflow, non-canonical byte, negative counts, retained base, live
slots, unsorted and unsigned thresholds, errors after prefix, move, real
callback, reentrancy and shared budget up to the last store. The instrumented
ordinary allocations of the fixtures remain zero; a real host backend may have
its own allocations.

```powershell
cmake --build build --config Release --target vmf_notification_state_test
ctest --test-dir build -C Release -R '^vmf_notification_state$' --output-on-failure
```

PE, Rea and DUMPBIN cross-check the three complete bodies, 153 bytes, against
the official primary on disk: 89BD90/47B, 89C210/73B, 889230/33B. No targeted
execution or memory observation of these routines is claimed.
Private evidence: `<private-evidence>/reprise-2026-10-31/vmf-notification-state-runtime-v1/`.
Four sources are frozen during the validation and six sensitive files
(project guidelines, executables, banks) remain unchanged.

An independent review reported no concrete defect on five sources/47882B; the
hashes are cross-checked separately. Its full report and oracles are not
adopted: they cite non-existent fields and phases and a non-concrete number of
steps. The project's tests and their real objects serve as the reference.
The check of the [child notification 89DD60](vmf_notification_child.md)
is now ported separately. The host binding of the full game, the
parent 89E060 and the sub-services remain to be hooked up. This batch does not
prove usable commercial menus, native D3D12 rendering, an online profile or a
working co-op.
