# Notification parent: 89E060

`VmfNotificationParentContext` links the C++ services already reconstructed:
child traversal, levels, deferred queue, toggle and localized display.
It replaces an earlier private proposal after corrections and tests.
The effects [89BF80](vmf_notification_effect_bf80.md) and
[89DB30](vmf_notification_effect_db30.md) are provided separately with their
concrete frames and their [shared pool](vmf_notification_effect_pool.md).
The connection to the complete hosts remains to be provided.

## Official reference

The complete body `89E060..89E209`, 426 bytes, matches between the primary
official PC MC 1.66 A executable, the secondary and the Rea export. SHA-256 of
the body:
`c3f44b5586d74e63e64cc000495fd0a1d4ad278b82a6c21889eb6580c048cc75`.
The primary keeps its SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Private evidence: `vmf-parent-runtime-v1/native-proof.json`, cross-checked on
disk and against the existing exports. No new native call or game trial.

## Order of operations

- Capture 204, then the global manager before evaluating 204. Check 205,
  the manager and its state 44; `0`, `0x12` and `0x18` end with AL0.
- In deferred mode, lock 1A8 before reading 208. A request already present
  ends without queueing. Otherwise capture the queue, write 208 then call the
  real `9B4490`. A full queue ignores the insertion; an error keeps 208.
- In synchronous mode, compute the level before the 2D4 store tied to the
  `0x2` mask of the mode. Traverse the children with the initial mode and
  accumulate their bytes by OR. Reread the base at each slot, then the count
  after each slot before the DWORD increment and the signed comparison. An
  unknown slot waits; NULL skips.
- Recompute the level. If it changes from zero, complete the real `8986B0(0)`
  before the +50 getter. `VmfNotificationParentToggleService` uses the
  concrete frame of the toggle. The real `889230` then recomputes from the new
  global +4C; it does not copy the previous level. Reread 3539, write 10 if
  needed, then traverse the children with mode 8.
- Select group 1/ordinal 0 only once. Capture display and owner before the
  following waits. Call the real `718120` with the ID64
  `0x00000000037BF702`, then return the OR accumulator.

The parent flag 3539 passed to a child is distinct from the global 16A2D2F
used by that child. A wait replaces no unknown binding with NULL and does not
request again a flag already captured.

## Lifetime and modern limits

The fields are logical, without imposing an x86 layout on the 64-bit client.
The +50 getter exposes a single progress object containing the encrypted
counter and the level destination; no pointer conversion between these fields.
The named and parent host facets must designate the same logical owner, with
their bindings updated consistently by the loader.

Prepare the pool of 1 to 32 frames and its owned registry before the
invocations. A duplicated identity, an incorrect State/Owner association or
pool exhaustion produces an explicit error. No convenience AL0 fake result.
Serialize accesses and keep entries, hosts, parameters and resources alive and
stable until all frames are handed back.

A deferred wait keeps the owner's recursive mutex. Resume, hand back and
destroy on its acquisition thread. A resumption on another thread is refused
without touching the lock; an incorrect destruction terminates the process
instead of performing an undefined unlock. Errors and abandonment keep the
writes already performed.

The eleven typed arguments of the formatter remain a modern boundary: the last
zero DWORD of the parent is the high half of the ID64, with no additional
formatting argument and no new evidence of the native bridge 416450.

## Validation

**52 suites pass in each architecture, x64 and Win32**, MSVC Release
C++latest, `/W4 /WX /permissive- /utf-8`. The 817 checks of the parent remain
active under `NDEBUG`. They cover guards, budgets, real locks, errors,
captures, unknown slots, live bases/counts, recomputation through another
host, real toggling, localized display and cancellation of nested frames.
The real dispatch also acquires and completes a real parent after assignment
and increment of an encrypted counter 41→42. Synthetic VMF data PC2.8/Xbox2.5;
remaining getters and effects explicitly synthetic in the tests.
The 69 tracked sources and six sensitive files are preserved.

```powershell
cmake --build build --config Release --target vmf_notification_parent_test
ctest --test-dir build -C Release -R '^vmf_notification_parent$' --output-on-failure
```

These module validations do not constitute playable commercial menus, a final
integrated rendering or a two-machine co-op session.
