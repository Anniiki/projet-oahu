# Child notification dispatch: 89E330

`VmfNotificationDispatchContext` wires together the display selectors, setters,
hashes and identity services already reconstructed. Its concrete adapter
`VmfNotificationDispatchFrame` lets the child control `89DD60` run it with
resumptions. The real parent `89E060` and the other effects remain required.

## Official evidence

Reference: PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The complete body `89E330..89E4E6`, 439 bytes, matches both the secondary's Rea
exports and the primary on disk. SHA-256:
`e31f7041288dbaa206e384f2be95c596dceff7bf24225830c91c54c6acde1801`.
The four strings referenced at `F161D0`, `F161E0`, `F161F0` and `F16204`
also match, totaling 60 bytes. Their texts come from the installation's
loader; they are not distributed in the sources.

Answers, Evidence identifiers and fingerprints stay out of Git in
`vmf-child-dispatch-runtime-v2/native-proof.json` and `data-proof.json`.
This cross-check reuses a Rea session that was already closed. No new game
launch, native call or game memory reading is performed.

## Branches and captures

The context reads the low ID, then the high ID, separately. The exact pairs
`FFFFFFFF/FFFFFFFF` and `00517180/0` return AL0 without any service. The other
non-zero high IDs, and the low IDs without a dedicated branch, return AL1.

| Low ID with a zero high ID | Effect |
| --- | --- |
| `52F820`, `578C00`, `560560` | Two `99A920` selections, then the real integer-to-text setter `440BA0`. |
| `5912A0`, `5A9940` | Host capture, global store, real `+4C` getter, flag store in the returned state. |
| `5C1FE0` | Identity lookup, assignment if absent, then parent call. |

The first display selection serves as a guard: NULL ends the call without a
name, a second selection or a write. After the name capture, the second selection
rereads the live registry. The setter keeps **this second display**; it does
not perform a third lookup. A second NULL result is a modern error, with no
fallback to the first display.

The flag branches capture the host before writing `10E7D34/35=1`, then call
that host after the store. The logical fields `3550/3551` belong to the object
actually returned. An unknown cell binding waits; a NULL cell is refused. A
captured NULL host keeps the global store already performed before the error,
without simulating a crash.

## Identities and parent

The identity path really executes `+24`, the `504930` hash, then `8911A0` on
the owned records and their explicit key. A match terminates immediately.
Otherwise, a **second** global `+24` getter and a second hash precede the real
`894D50` context. The name keeps its captured metadata; its bytes and the
hashing mode stay live. The two owners may differ. A NULL owner fails at its
first use after the hash, without becoming an absence of identity.

The `+50` getter of the assignment comes from the same live global host,
projected through the C++ interfaces; no conversion of a counter into a state
is used. The host waits only if the assignment reaches that getter. Its AL0
and AL1 results are ignored after its real end; its errors remain terminal.

A new global `+4C` getter precedes `begin_parent(state, 7, 0)`. State, service
and parent frame are captured without replay during waits. The interface
requires a real implementation of the parent, still missing. A NULL frame
means a real synchronous end, never mere availability. The borrowed frame is
detached before its single release, without rollback.

## Adapter and verification

The concrete frame runs this context and refuses a second active borrower. It
keeps the typed error after release for diagnostics. Keep inputs, objects,
keys, modules and frames alive and stable until the end of their borrow,
including abandonment. Serialize accesses: the reentry guards are not mutexes.
These classes do not allocate from the ordinary heap; their dependencies may
allocate.

**50 targeted suites pass on each architecture, x64 and Win32**, MSVC Release
C++latest, `/W4 /WX /permissive- /utf-8`. The 192 dispatch checks cover the
two synthetic VMF profiles PC 2.8/Xbox 2.5, mutations between lookups and
calls, real encrypted identity/counter changes, AL0 assignment, parent captures
and the effective composition `89DD60 → DispatchFrame → 89E330 → setter VMF`.
The 62 tracked sources and six sensitive files remain unchanged during
validation.

```powershell
cmake --build build --config Release --target vmf_notification_dispatch_test
ctest --test-dir build -C Release -R '^vmf_notification_dispatch$' --output-on-failure
```

The test data, getters and parent are synthetic. These results do not yet
validate playable commercial menus or co-op. The `416450` bridge remains
corroborated on the secondary only: its 82 bytes differ at the same RVA in
the primary. The eleven typed slots form an explicit x64 boundary, without
emulating uninitialized x86 stack words.
